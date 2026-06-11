/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CHANNEL_MANAGER_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CHANNEL_MANAGER_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "acl/acl_rt.h"
#include "common/hixl_utils.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_memory.h"
#include "fabric_mem/fabric_mem_slot_pool.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {

struct FabricMemChannel {
  std::unique_ptr<FabricMemRemoteMemory> remote_memory;
  int32_t keepalive_fd{-1};

  // records_mutex protects the in-flight transfer bookkeeping below. Copy submission, the authoritative
  // disconnecting check, and disconnect's abort all run under this mutex so that aborting an in-flight
  // transfer is mutually exclusive with submitting new work (abort-before-unmap safety). disconnecting
  // is atomic so the transfer fast path can cheaply reject a disconnecting channel before doing the
  // (lock-free) address resolution; the authoritative re-check still happens under records_mutex.
  std::mutex records_mutex;
  std::atomic<bool> disconnecting{false};
  std::unordered_map<uint64_t, AsyncRecord> async_records;
  std::vector<AsyncSlot> active_sync_slots;
};

struct FabricMemChannelManagerInitParam {
  std::string local_engine;
  int32_t device_id{-1};
  bool auto_connect{false};
  FabricMemStatistic *statistic{nullptr};
  FabricMemSlotPool *slot_pool{nullptr};
  FabricMemControlServer *control_server{nullptr};
  aclrtContext aclrt_context{nullptr};
};

class FabricMemChannelManager {
 public:
  FabricMemChannelManager() = default;
  ~FabricMemChannelManager();
  FabricMemChannelManager(const FabricMemChannelManager &) = delete;
  FabricMemChannelManager &operator=(const FabricMemChannelManager &) = delete;
  FabricMemChannelManager(FabricMemChannelManager &&) = delete;
  FabricMemChannelManager &operator=(FabricMemChannelManager &&) = delete;

  Status Initialize(const FabricMemChannelManagerInitParam &param);
  void Finalize();
  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis);
  void DisconnectAll();
  Status EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status GetChannel(const std::string &remote_engine, std::shared_ptr<FabricMemChannel> &channel) const;
  Status BuildTransferContext(const std::string &remote_engine, FabricMemStatistic *statistic,
                              FabricMemTransferContext &context);
  bool HasChannels() const;
  bool IsConnected(const std::string &remote_engine) const;
  Status StartKeepaliveMonitor();
  void StopKeepaliveMonitor();
  static void SetKeepaliveCheckIntervalMs(int64_t interval_ms);

  // Async request routing: maps a request id to the channel that owns its AsyncRecord.
  void AddReqRoute(uint64_t req_id, const std::shared_ptr<FabricMemChannel> &channel);
  Status FindChannelByReq(uint64_t req_id, std::shared_ptr<FabricMemChannel> &channel) const;
  void RemoveReqRoute(uint64_t req_id);

 private:
  Status FetchAndInstallRemote(const std::string &remote, int32_t timeout_in_millis, Status if_already_connected);
  Status CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles, const std::string &remote);
  Status DisconnectRemote(const AscendString &remote_engine, int32_t timeout_in_millis);
  void AbortAndClearChannelRecords(const std::shared_ptr<FabricMemChannel> &channel);
  void RemoveChannelEntryLocked(const std::string &remote);
  void CleanupChannelsLocked();
  void CheckKeepaliveFds();
  void SendOutboundHeartbeats();
  void DisconnectDeadRemote(const std::string &remote);

  // connect_mutex_: serializes Fetch + remote install across Connect/EnsureConnected.
  // Never hold channels_mutex_ during network I/O.
  std::mutex connect_mutex_;

  // channels_mutex_: protects channels_ and initialized_.
  mutable std::mutex channels_mutex_;
  std::unordered_map<std::string, std::shared_ptr<FabricMemChannel>> channels_;

  // req_route_mutex_: protects req_2_channel_. It is a leaf lock: when held together with a channel's
  // records_mutex the fixed order is records_mutex -> req_route_mutex_ (e.g. IssueAsyncCopyAndRegister
  // calls AddReqRoute while holding records_mutex); the reverse order is forbidden.
  mutable std::mutex req_route_mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<FabricMemChannel>> req_2_channel_;

  std::string local_engine_;
  int32_t device_id_{-1};
  bool auto_connect_{false};
  bool initialized_{false};
  FabricMemStatistic *statistic_{nullptr};
  FabricMemSlotPool *slot_pool_{nullptr};
  FabricMemControlServer *control_server_{nullptr};
  aclrtContext aclrt_context_{nullptr};

  std::thread keepalive_monitor_;
  std::atomic<bool> keepalive_stop_{false};
  std::mutex keepalive_cv_mutex_;
  std::condition_variable keepalive_cv_;
  static int64_t keepalive_check_interval_ms_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_CHANNEL_MANAGER_H_
