/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_STATISTIC_MANAGER_H_
#define CANN_HIXL_SRC_HIXL_CS_STATISTIC_MANAGER_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace hixl {

struct CostStatisticInfo {
  std::atomic<uint64_t> times{0UL};
  std::atomic<uint64_t> max_cost{0UL};
  std::atomic<uint64_t> total_cost{0UL};

  void Reset();
};

struct TransferStatisticInfo {
  CostStatisticInfo transfer_total;
  CostStatisticInfo transfer_submit;
  CostStatisticInfo transfer_wait_complete;
  CostStatisticInfo check_status_host;
  CostStatisticInfo check_status_device;
  std::atomic<uint64_t> total_bytes{0UL};
  std::atomic<uint64_t> total_op_desc_count{0UL};
  std::atomic<uint64_t> host_transfer_times{0UL};
  std::atomic<uint64_t> device_transfer_times{0UL};

  void Reset();
};

struct ConnectStatisticInfo {
  CostStatisticInfo connect_total;
  CostStatisticInfo tcp_connect;
  CostStatisticInfo match_endpoint;
  CostStatisticInfo get_remote_mem_total;
  CostStatisticInfo import_remote_mem;
  CostStatisticInfo create_channel_req;
  CostStatisticInfo local_create_channel;
  CostStatisticInfo wait_create_channel_resp;

  void Reset();
};

struct ServerStatisticInfo {
  CostStatisticInfo initialize;
  CostStatisticInfo listen;
  CostStatisticInfo server_match_endpoint;
  CostStatisticInfo server_create_channel;
  CostStatisticInfo server_export_mem;
  CostStatisticInfo server_destroy_channel;
  CostStatisticInfo server_cleanup_client;
  CostStatisticInfo finalize;

  void Reset();
};

struct StatisticInfo {
  ConnectStatisticInfo connect;
  TransferStatisticInfo transfer;
  ServerStatisticInfo server;

  void Reset();
};

struct CostStatisticSnapshot {
  uint64_t times{0UL};
  uint64_t max_cost{0UL};
  uint64_t total_cost{0UL};
};

struct TransferStatisticSnapshot {
  CostStatisticSnapshot transfer_total;
  CostStatisticSnapshot transfer_submit;
  CostStatisticSnapshot transfer_wait_complete;
  CostStatisticSnapshot check_status_host;
  CostStatisticSnapshot check_status_device;
  uint64_t total_bytes{0UL};
  uint64_t total_op_desc_count{0UL};
  uint64_t host_transfer_times{0UL};
  uint64_t device_transfer_times{0UL};
};

struct ConnectStatisticSnapshot {
  CostStatisticSnapshot connect_total;
  CostStatisticSnapshot tcp_connect;
  CostStatisticSnapshot match_endpoint;
  CostStatisticSnapshot get_remote_mem_total;
  CostStatisticSnapshot import_remote_mem;
  CostStatisticSnapshot create_channel_req;
  CostStatisticSnapshot local_create_channel;
  CostStatisticSnapshot wait_create_channel_resp;
};

struct ServerStatisticSnapshot {
  CostStatisticSnapshot initialize;
  CostStatisticSnapshot listen;
  CostStatisticSnapshot server_match_endpoint;
  CostStatisticSnapshot server_create_channel;
  CostStatisticSnapshot server_export_mem;
  CostStatisticSnapshot server_destroy_channel;
  CostStatisticSnapshot server_cleanup_client;
  CostStatisticSnapshot finalize;
};

struct StatisticInfoSnapshot {
  ConnectStatisticSnapshot connect;
  TransferStatisticSnapshot transfer;
  ServerStatisticSnapshot server;
};

enum class StatisticStage : uint32_t {
  kConnectTotal = 0U,
  kTcpConnect,
  kMatchEndpoint,
  kGetRemoteMemTotal,
  kImportRemoteMem,
  kCreateChannelReq,
  kLocalCreateChannel,
  kWaitCreateChannelResp,
  kTransferTotal,
  kTransferSubmit,
  kTransferWaitComplete,
  kCheckStatusHost,
  kCheckStatusDevice,
  kServerInitialize,
  kServerListen,
  kServerMatchEndpoint,
  kServerCreateChannel,
  kServerExportMem,
  kServerDestroyChannel,
  kServerCleanupClient,
  kServerFinalize,
};

class HixlCSStatisticManager {
 public:
  static HixlCSStatisticManager &GetInstance();
  ~HixlCSStatisticManager();

  HixlCSStatisticManager(const HixlCSStatisticManager &) = delete;
  HixlCSStatisticManager(const HixlCSStatisticManager &&) = delete;
  HixlCSStatisticManager &operator=(const HixlCSStatisticManager &) = delete;
  HixlCSStatisticManager &operator=(const HixlCSStatisticManager &&) = delete;

  static std::string GetClientChannelId(const std::string &peer);
  static std::string GetServerChannelId(const std::string &peer);

  void RegisterChannel(const std::string &channel_id);
  void RemoveChannel(const std::string &channel_id);
  void StartPeriodicDumpIfNeeded();
  void Dump();
  void UpdateStageCost(const std::string &channel_id, StatisticStage stage, uint64_t cost_us);
  void UpdateTransferCost(const std::string &channel_id, uint64_t total_cost_us, uint64_t submit_cost_us,
                          uint64_t wait_cost_us, uint64_t total_bytes, uint64_t op_desc_count, bool is_device);
  StatisticInfoSnapshot GetSnapshot(const std::string &channel_id) const;

 private:
  HixlCSStatisticManager() = default;
  static void UpdateCost(uint64_t cost, std::atomic<uint64_t> &times, std::atomic<uint64_t> &max_cost,
                         std::atomic<uint64_t> &total_cost);
  static CostStatisticSnapshot ToSnapshot(const CostStatisticInfo &info);
  static uint64_t GetAvgCost(const CostStatisticInfo &info);
  std::shared_ptr<StatisticInfo> GetOrCreateStatisticInfo(const std::string &channel_id);
  std::shared_ptr<StatisticInfo> GetStatisticInfo(const std::string &channel_id) const;
  void DumpConnectStatisticInfo();
  void DumpTransferStatisticInfo();
  void DumpServerStatisticInfo();
  void DumpThreadMain();

  std::atomic<bool> dump_running_{false};
  std::mutex dump_mutex_;
  std::condition_variable dump_cv_;
  std::thread dump_thread_;
  mutable std::shared_mutex map_mutex_;
  std::unordered_map<std::string, std::shared_ptr<StatisticInfo>> statistic_info_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_STATISTIC_MANAGER_H_
