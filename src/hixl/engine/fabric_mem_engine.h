/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_
#define HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/hixl_inner_types.h"
#include "engine.h"
#include "fabric_mem/fabric_mem_config.h"
#include "fabric_mem/fabric_mem_control.h"
#include "fabric_mem/fabric_mem_remote_memory.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#include "hixl/hixl_types.h"

#include "acl/acl.h"

namespace hixl {
struct RemoteConnection {
  std::unique_ptr<FabricMemRemoteMemory> remote_memory;
  std::mutex state_mutex;
  std::condition_variable cv;
  uint32_t in_flight{0U};
  bool disconnecting{false};
};

struct FabricMemTransferRequest {
  TransferInfo info;
  std::shared_ptr<RemoteConnection> conn;
};

class FabricMemEngine : public hixl::Engine {
 public:
  explicit FabricMemEngine(const AscendString &local_engine) : Engine(local_engine){};

  ~FabricMemEngine() override = default;

  Status Initialize(const std::map<AscendString, AscendString> &options) override;
  void Finalize() override;
  bool IsInitialized() const override;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) override;
  Status DeregisterMem(MemHandle mem_handle) override;
  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis) override;
  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) override;
  void Disconnect() override;

  Status TransferSync(const AscendString &remote_engine, TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) override;
  Status TransferAsync(const AscendString &remote_engine, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                       TransferReq &req) override;
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status) override;
  Status GetTransferStatus(const GetTransferStatusArgs &args, std::vector<TransferResult> &results) override;

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000) override;
  Status GetNotifies(std::vector<NotifyDesc> &notifies) override;
  Status RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) override;

 private:
  class RemoteTransferLease {
   public:
    RemoteTransferLease(FabricMemEngine &engine, std::shared_ptr<RemoteConnection> conn, bool acquired)
        : engine_(engine), conn_(std::move(conn)), acquired_(acquired) {}
    ~RemoteTransferLease() {
      if (acquired_) {
        engine_.ReleaseTransferLease(conn_);
      }
    }
    RemoteTransferLease(const RemoteTransferLease &) = delete;
    RemoteTransferLease &operator=(const RemoteTransferLease &) = delete;
    RemoteTransferLease(RemoteTransferLease &&other) noexcept
        : engine_(other.engine_), conn_(std::move(other.conn_)), acquired_(other.acquired_) {
      other.acquired_ = false;
    }
    RemoteTransferLease &operator=(RemoteTransferLease &&) = delete;
    bool Acquired() const {
      return acquired_;
    }
    void Release() {
      if (acquired_) {
        engine_.ReleaseTransferLease(conn_);
        acquired_ = false;
      }
    }

   private:
    FabricMemEngine &engine_;
    std::shared_ptr<RemoteConnection> conn_;
    bool acquired_;
  };

  Status ApplyVirtualMemoryConfig();
  Status InitTransferService();
  Status StartControlServer();
  Status InitFabricMem();
  Status AcquireVirtualMemoryManager();
  void ReleaseVirtualMemoryManager();
  void CleanupFabricMemLocked();
  bool HasConnectionsLocked() const;
  Status LookupRemoteConnectionLocked(const std::string &remote_engine, std::shared_ptr<RemoteConnection> &conn);
  Status BuildTransferContext(const RemoteConnection &conn, const std::string &remote_engine,
                              FabricMemTransferContext &context);
  Status AcquireTransferLease(const std::string &remote_engine, std::shared_ptr<RemoteConnection> &conn);
  void ReleaseTransferLease(const std::shared_ptr<RemoteConnection> &conn);
  Status DisconnectLocked(const AscendString &remote_engine, int32_t timeout_in_millis,
                          bool wait_in_flight = true);
  // Ensures connection to remote without holding mutex_ during network I/O.
  Status EnsureConnected(const AscendString &remote_engine, int32_t timeout_in_millis);
  Status CreateAndRegisterRemoteMemory(const std::vector<ShareHandleInfo> &share_handles, const std::string &remote);
  void RemoveChannelReqMapLocked(const std::string &remote_engine);
  Status DisconnectOnTransferError(const AscendString &remote_engine, int32_t timeout_in_millis);

  // mutex_: engine lifecycle, mem_map_, remote connection table, keepalive_fds_.
  // connection_mutex_: serializes remote install after network fetch.
  // req_map_mutex_: protects req_map_.
  // RemoteConnection::state_mutex: per-remote transfer/disconnect exclusion.
  std::mutex mutex_;
  std::mutex connection_mutex_;
  std::mutex req_map_mutex_;
  std::atomic<bool> is_initialized_{false};

  FabricMemConfig fabric_mem_config_;
  FabricMemStatistic fabric_mem_statistic_;
  std::shared_ptr<FabricMemTransferService> fabric_mem_transfer_service_;
  std::unique_ptr<FabricMemControlServer> fabric_mem_control_server_;
  std::unordered_map<std::string, std::shared_ptr<RemoteConnection>> fabric_mem_remote_mems_;
  std::unordered_map<std::string, int32_t> keepalive_fds_;
  std::unordered_map<void *, MemInfo> mem_map_;
  std::unordered_map<uint64_t, FabricMemTransferRequest> req_map_;
  std::atomic<uint64_t> next_req_id_{1U};
  bool has_acquired_virtual_memory_ = false;
  bool auto_connect_{false};
  int32_t device_id_{-1};
  aclrtContext aclrt_context_{nullptr};
};
}  // namespace hixl

#endif  // HIXL_SRC_HIXL_ENGINE_FABRIC_MEM_ENGINE_H_
