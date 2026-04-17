/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "statistic_manager.h"

#include <algorithm>
#include <chrono>
#include <utility>
#include "common/hixl_log.h"

namespace hixl {
namespace {
constexpr uint64_t kResetTimes = 100000UL;
constexpr uint32_t kStatisticDumpPeriodMs = 80U * 1000U;
constexpr double kBytesPerGb = 1024.0 * 1024.0 * 1024.0;
constexpr double kMicrosPerSecond = 1000.0 * 1000.0;
constexpr uint64_t kBytesPerKBytes = 1024UL;
constexpr char kClientPrefix[] = "client:";
constexpr char kServerPrefix[] = "server:";

double GetBandwidthGbps(uint64_t total_bytes, uint64_t total_cost_us) {
  if (total_bytes == 0U || total_cost_us == 0U) {
    return 0.0;
  }
  return static_cast<double>(total_bytes) * kMicrosPerSecond / static_cast<double>(total_cost_us) / kBytesPerGb;
}

uint64_t ToKBytes(uint64_t bytes) {
  return bytes / kBytesPerKBytes;
}

uint64_t GetAvgSize(uint64_t total_bytes, uint64_t total_op_desc_count) {
  if (total_bytes == 0U || total_op_desc_count == 0U) {
    return 0U;
  }
  return total_bytes / total_op_desc_count;
}
}  // namespace

void CostStatisticInfo::Reset() {
  times.store(0UL);
  max_cost.store(0UL);
  total_cost.store(0UL);
}

void TransferStatisticInfo::Reset() {
  transfer_total.Reset();
  transfer_submit.Reset();
  transfer_wait_complete.Reset();
  check_status_host.Reset();
  check_status_device.Reset();
  total_bytes.store(0UL);
  total_op_desc_count.store(0UL);
  host_transfer_times.store(0UL);
  device_transfer_times.store(0UL);
}

void ConnectStatisticInfo::Reset() {
  connect_total.Reset();
  tcp_connect.Reset();
  match_endpoint.Reset();
  get_remote_mem_total.Reset();
  import_remote_mem.Reset();
  create_channel_req.Reset();
  local_create_channel.Reset();
  wait_create_channel_resp.Reset();
}

void ServerStatisticInfo::Reset() {
  initialize.Reset();
  listen.Reset();
  server_match_endpoint.Reset();
  server_create_channel.Reset();
  server_export_mem.Reset();
  server_destroy_channel.Reset();
  server_cleanup_client.Reset();
  finalize.Reset();
}

void StatisticInfo::Reset() {
  connect.Reset();
  transfer.Reset();
  server.Reset();
}

HixlCSStatisticManager &HixlCSStatisticManager::GetInstance() {
  static HixlCSStatisticManager instance;
  return instance;
}

HixlCSStatisticManager::~HixlCSStatisticManager() {
  {
    std::lock_guard<std::mutex> lock(dump_mutex_);
    dump_running_.store(false);
  }
  dump_cv_.notify_all();
  if (dump_thread_.joinable()) {
    dump_thread_.join();
  }
}

std::string HixlCSStatisticManager::GetClientChannelId(const std::string &peer) {
  return kClientPrefix + peer;
}

std::string HixlCSStatisticManager::GetServerChannelId(const std::string &peer) {
  return kServerPrefix + peer;
}

void HixlCSStatisticManager::RegisterChannel(const std::string &channel_id) {
  (void)GetOrCreateStatisticInfo(channel_id);
}

void HixlCSStatisticManager::RemoveChannel(const std::string &channel_id) {
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  statistic_info_.erase(channel_id);
}

void HixlCSStatisticManager::StartPeriodicDumpIfNeeded() {
  std::lock_guard<std::mutex> lock(dump_mutex_);
  if (dump_running_.load()) {
    return;
  }
  dump_running_.store(true);
  dump_thread_ = std::thread([this]() { DumpThreadMain(); });
}

void HixlCSStatisticManager::DumpThreadMain() {
  std::unique_lock<std::mutex> lock(dump_mutex_);
  while (dump_running_.load()) {
    if (dump_cv_.wait_for(lock, std::chrono::milliseconds(kStatisticDumpPeriodMs),
                          [this]() { return !dump_running_.load(); })) {
      break;
    }
    lock.unlock();
    Dump();
    lock.lock();
  }
}

void HixlCSStatisticManager::UpdateCost(uint64_t cost, std::atomic<uint64_t> &times, std::atomic<uint64_t> &max_cost,
                                        std::atomic<uint64_t> &total_cost) {
  (void)times.fetch_add(1U, std::memory_order_relaxed);
  (void)total_cost.fetch_add(cost, std::memory_order_relaxed);
  auto current_max = max_cost.load(std::memory_order_relaxed);
  while (current_max < cost &&
         !max_cost.compare_exchange_weak(current_max, cost, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
}

CostStatisticSnapshot HixlCSStatisticManager::ToSnapshot(const CostStatisticInfo &info) {
  return {info.times.load(std::memory_order_relaxed), info.max_cost.load(std::memory_order_relaxed),
          info.total_cost.load(std::memory_order_relaxed)};
}

uint64_t HixlCSStatisticManager::GetAvgCost(const CostStatisticInfo &info) {
  const uint64_t times = info.times.load(std::memory_order_relaxed);
  if (times == 0U) {
    return 0U;
  }
  return info.total_cost.load(std::memory_order_relaxed) / times;
}

std::shared_ptr<StatisticInfo> HixlCSStatisticManager::GetOrCreateStatisticInfo(const std::string &channel_id) {
  {
    std::shared_lock<std::shared_mutex> lock(map_mutex_);
    const auto it = statistic_info_.find(channel_id);
    if (it != statistic_info_.end()) {
      return it->second;
    }
  }

  auto info = std::make_shared<StatisticInfo>();
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  auto [it, inserted] = statistic_info_.emplace(channel_id, info);
  return inserted ? info : it->second;
}

std::shared_ptr<StatisticInfo> HixlCSStatisticManager::GetStatisticInfo(const std::string &channel_id) const {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  const auto it = statistic_info_.find(channel_id);
  if (it == statistic_info_.end()) {
    return nullptr;
  }
  return it->second;
}

void HixlCSStatisticManager::UpdateStageCost(const std::string &channel_id, StatisticStage stage, uint64_t cost_us) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  switch (stage) {
    case StatisticStage::kConnectTotal:
      UpdateCost(cost_us, info->connect.connect_total.times, info->connect.connect_total.max_cost,
                 info->connect.connect_total.total_cost);
      break;
    case StatisticStage::kTcpConnect:
      UpdateCost(cost_us, info->connect.tcp_connect.times, info->connect.tcp_connect.max_cost,
                 info->connect.tcp_connect.total_cost);
      break;
    case StatisticStage::kMatchEndpoint:
      UpdateCost(cost_us, info->connect.match_endpoint.times, info->connect.match_endpoint.max_cost,
                 info->connect.match_endpoint.total_cost);
      break;
    case StatisticStage::kGetRemoteMemTotal:
      UpdateCost(cost_us, info->connect.get_remote_mem_total.times, info->connect.get_remote_mem_total.max_cost,
                 info->connect.get_remote_mem_total.total_cost);
      break;
    case StatisticStage::kImportRemoteMem:
      UpdateCost(cost_us, info->connect.import_remote_mem.times, info->connect.import_remote_mem.max_cost,
                 info->connect.import_remote_mem.total_cost);
      break;
    case StatisticStage::kCreateChannelReq:
      UpdateCost(cost_us, info->connect.create_channel_req.times, info->connect.create_channel_req.max_cost,
                 info->connect.create_channel_req.total_cost);
      break;
    case StatisticStage::kLocalCreateChannel:
      UpdateCost(cost_us, info->connect.local_create_channel.times, info->connect.local_create_channel.max_cost,
                 info->connect.local_create_channel.total_cost);
      break;
    case StatisticStage::kWaitCreateChannelResp:
      UpdateCost(cost_us, info->connect.wait_create_channel_resp.times, info->connect.wait_create_channel_resp.max_cost,
                 info->connect.wait_create_channel_resp.total_cost);
      break;
    case StatisticStage::kTransferWaitComplete:
      UpdateCost(cost_us, info->transfer.transfer_wait_complete.times, info->transfer.transfer_wait_complete.max_cost,
                 info->transfer.transfer_wait_complete.total_cost);
      break;
    case StatisticStage::kCheckStatusHost:
      UpdateCost(cost_us, info->transfer.check_status_host.times, info->transfer.check_status_host.max_cost,
                 info->transfer.check_status_host.total_cost);
      break;
    case StatisticStage::kCheckStatusDevice:
      UpdateCost(cost_us, info->transfer.check_status_device.times, info->transfer.check_status_device.max_cost,
                 info->transfer.check_status_device.total_cost);
      break;
    case StatisticStage::kServerInitialize:
      UpdateCost(cost_us, info->server.initialize.times, info->server.initialize.max_cost, info->server.initialize.total_cost);
      break;
    case StatisticStage::kServerListen:
      UpdateCost(cost_us, info->server.listen.times, info->server.listen.max_cost, info->server.listen.total_cost);
      break;
    case StatisticStage::kServerMatchEndpoint:
      UpdateCost(cost_us, info->server.server_match_endpoint.times, info->server.server_match_endpoint.max_cost,
                 info->server.server_match_endpoint.total_cost);
      break;
    case StatisticStage::kServerCreateChannel:
      UpdateCost(cost_us, info->server.server_create_channel.times, info->server.server_create_channel.max_cost,
                 info->server.server_create_channel.total_cost);
      break;
    case StatisticStage::kServerExportMem:
      UpdateCost(cost_us, info->server.server_export_mem.times, info->server.server_export_mem.max_cost,
                 info->server.server_export_mem.total_cost);
      break;
    case StatisticStage::kServerDestroyChannel:
      UpdateCost(cost_us, info->server.server_destroy_channel.times, info->server.server_destroy_channel.max_cost,
                 info->server.server_destroy_channel.total_cost);
      break;
    case StatisticStage::kServerCleanupClient:
      UpdateCost(cost_us, info->server.server_cleanup_client.times, info->server.server_cleanup_client.max_cost,
                 info->server.server_cleanup_client.total_cost);
      break;
    case StatisticStage::kServerFinalize:
      UpdateCost(cost_us, info->server.finalize.times, info->server.finalize.max_cost, info->server.finalize.total_cost);
      break;
    case StatisticStage::kTransferTotal:
    case StatisticStage::kTransferSubmit:
      break;
  }

  if (info->connect.connect_total.times.load(std::memory_order_relaxed) > kResetTimes) {
    info->connect.Reset();
  }
  if (info->transfer.transfer_total.times.load(std::memory_order_relaxed) > kResetTimes) {
    info->transfer.Reset();
  }
  if (info->server.server_match_endpoint.times.load(std::memory_order_relaxed) > kResetTimes) {
    info->server.Reset();
  }
}

void HixlCSStatisticManager::UpdateTransferCost(const std::string &channel_id, uint64_t total_cost_us,
                                                uint64_t submit_cost_us, uint64_t wait_cost_us, uint64_t total_bytes,
                                                uint64_t op_desc_count, bool is_device) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(total_cost_us, info->transfer.transfer_total.times, info->transfer.transfer_total.max_cost,
             info->transfer.transfer_total.total_cost);
  UpdateCost(submit_cost_us, info->transfer.transfer_submit.times, info->transfer.transfer_submit.max_cost,
             info->transfer.transfer_submit.total_cost);
  UpdateCost(wait_cost_us, info->transfer.transfer_wait_complete.times, info->transfer.transfer_wait_complete.max_cost,
             info->transfer.transfer_wait_complete.total_cost);
  (void)info->transfer.total_bytes.fetch_add(total_bytes, std::memory_order_relaxed);
  (void)info->transfer.total_op_desc_count.fetch_add(op_desc_count, std::memory_order_relaxed);
  if (is_device) {
    (void)info->transfer.device_transfer_times.fetch_add(1U, std::memory_order_relaxed);
  } else {
    (void)info->transfer.host_transfer_times.fetch_add(1U, std::memory_order_relaxed);
  }
  if (info->transfer.transfer_total.times.load(std::memory_order_relaxed) > kResetTimes) {
    info->transfer.Reset();
  }
}

StatisticInfoSnapshot HixlCSStatisticManager::GetSnapshot(const std::string &channel_id) const {
  StatisticInfoSnapshot snapshot;
  const auto info = GetStatisticInfo(channel_id);
  if (info == nullptr) {
    return snapshot;
  }
  snapshot.connect.connect_total = ToSnapshot(info->connect.connect_total);
  snapshot.connect.tcp_connect = ToSnapshot(info->connect.tcp_connect);
  snapshot.connect.match_endpoint = ToSnapshot(info->connect.match_endpoint);
  snapshot.connect.get_remote_mem_total = ToSnapshot(info->connect.get_remote_mem_total);
  snapshot.connect.import_remote_mem = ToSnapshot(info->connect.import_remote_mem);
  snapshot.connect.create_channel_req = ToSnapshot(info->connect.create_channel_req);
  snapshot.connect.local_create_channel = ToSnapshot(info->connect.local_create_channel);
  snapshot.connect.wait_create_channel_resp = ToSnapshot(info->connect.wait_create_channel_resp);
  snapshot.transfer.transfer_total = ToSnapshot(info->transfer.transfer_total);
  snapshot.transfer.transfer_submit = ToSnapshot(info->transfer.transfer_submit);
  snapshot.transfer.transfer_wait_complete = ToSnapshot(info->transfer.transfer_wait_complete);
  snapshot.transfer.check_status_host = ToSnapshot(info->transfer.check_status_host);
  snapshot.transfer.check_status_device = ToSnapshot(info->transfer.check_status_device);
  snapshot.transfer.total_bytes = info->transfer.total_bytes.load(std::memory_order_relaxed);
  snapshot.transfer.total_op_desc_count = info->transfer.total_op_desc_count.load(std::memory_order_relaxed);
  snapshot.transfer.host_transfer_times = info->transfer.host_transfer_times.load(std::memory_order_relaxed);
  snapshot.transfer.device_transfer_times = info->transfer.device_transfer_times.load(std::memory_order_relaxed);
  snapshot.server.initialize = ToSnapshot(info->server.initialize);
  snapshot.server.listen = ToSnapshot(info->server.listen);
  snapshot.server.server_match_endpoint = ToSnapshot(info->server.server_match_endpoint);
  snapshot.server.server_create_channel = ToSnapshot(info->server.server_create_channel);
  snapshot.server.server_export_mem = ToSnapshot(info->server.server_export_mem);
  snapshot.server.server_destroy_channel = ToSnapshot(info->server.server_destroy_channel);
  snapshot.server.server_cleanup_client = ToSnapshot(info->server.server_cleanup_client);
  snapshot.server.finalize = ToSnapshot(info->server.finalize);
  return snapshot;
}

void HixlCSStatisticManager::Dump() {
  DumpConnectStatisticInfo();
  DumpTransferStatisticInfo();
  DumpServerStatisticInfo();
}

void HixlCSStatisticManager::DumpConnectStatisticInfo() {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : statistic_info_) {
    const auto &connect = item.second->connect;
    if (connect.connect_total.times.load(std::memory_order_relaxed) == 0U &&
        connect.match_endpoint.times.load(std::memory_order_relaxed) == 0U &&
        connect.get_remote_mem_total.times.load(std::memory_order_relaxed) == 0U) {
      continue;
    }
    HIXL_EVENT("HIXL CS connect statistic info[channel:%s, total times:%" PRIu64 ", max cost:%" PRIu64
               " us, avg cost:%" PRIu64 " us, tcp avg cost:%" PRIu64 " us, match avg cost:%" PRIu64
               " us, get_remote_mem avg cost:%" PRIu64 " us, import avg cost:%" PRIu64
               " us, create_req avg cost:%" PRIu64 " us, local_create avg cost:%" PRIu64
               " us, wait_resp avg cost:%" PRIu64 " us]",
               item.first.c_str(), connect.connect_total.times.load(std::memory_order_relaxed),
               connect.connect_total.max_cost.load(std::memory_order_relaxed), GetAvgCost(connect.connect_total),
               GetAvgCost(connect.tcp_connect), GetAvgCost(connect.match_endpoint), GetAvgCost(connect.get_remote_mem_total),
               GetAvgCost(connect.import_remote_mem), GetAvgCost(connect.create_channel_req),
               GetAvgCost(connect.local_create_channel), GetAvgCost(connect.wait_create_channel_resp));
    }
}

void HixlCSStatisticManager::DumpTransferStatisticInfo() {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : statistic_info_) {
    const auto &transfer = item.second->transfer;
    if (transfer.transfer_total.times.load(std::memory_order_relaxed) == 0U &&
        transfer.check_status_host.times.load(std::memory_order_relaxed) == 0U &&
        transfer.check_status_device.times.load(std::memory_order_relaxed) == 0U) {
      continue;
    }
    const uint64_t total_bytes = transfer.total_bytes.load(std::memory_order_relaxed);
    const uint64_t total_cost = transfer.transfer_total.total_cost.load(std::memory_order_relaxed);
    const uint64_t total_ops = transfer.total_op_desc_count.load(std::memory_order_relaxed);
    HIXL_EVENT("HIXL CS transfer statistic info[channel:%s, transfer times:%" PRIu64 ", total size:%" PRIu64
               " kBytes, avg size:%" PRIu64 " kBytes, max cost:%" PRIu64 " us, avg total cost:%" PRIu64
               " us, avg submit cost:%" PRIu64 " us, avg wait cost:%" PRIu64 " us, avg host query cost:%" PRIu64
               " us, avg device query cost:%" PRIu64 " us, bandwidth:%.3f GB/s, host times:%" PRIu64
               ", device times:%" PRIu64 ", total op_desc:%" PRIu64 "]",
               item.first.c_str(), transfer.transfer_total.times.load(std::memory_order_relaxed), ToKBytes(total_bytes),
               ToKBytes(GetAvgSize(total_bytes, total_ops)), transfer.transfer_total.max_cost.load(std::memory_order_relaxed),
               GetAvgCost(transfer.transfer_total), GetAvgCost(transfer.transfer_submit),
               GetAvgCost(transfer.transfer_wait_complete), GetAvgCost(transfer.check_status_host),
               GetAvgCost(transfer.check_status_device), GetBandwidthGbps(total_bytes, total_cost),
               transfer.host_transfer_times.load(std::memory_order_relaxed),
               transfer.device_transfer_times.load(std::memory_order_relaxed), total_ops);
  }
}

void HixlCSStatisticManager::DumpServerStatisticInfo() {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : statistic_info_) {
    const auto &server = item.second->server;
    if (server.initialize.times.load(std::memory_order_relaxed) == 0U &&
        server.server_match_endpoint.times.load(std::memory_order_relaxed) == 0U &&
        server.server_create_channel.times.load(std::memory_order_relaxed) == 0U &&
        server.server_export_mem.times.load(std::memory_order_relaxed) == 0U &&
        server.server_cleanup_client.times.load(std::memory_order_relaxed) == 0U) {
      continue;
    }
    HIXL_EVENT("HIXL CS server statistic info[channel:%s, init avg cost:%" PRIu64 " us, listen avg cost:%" PRIu64
               " us, match avg cost:%" PRIu64 " us, create avg cost:%" PRIu64 " us, export avg cost:%" PRIu64
               " us, destroy avg cost:%" PRIu64 " us, cleanup avg cost:%" PRIu64 " us, finalize avg cost:%" PRIu64
               " us]",
               item.first.c_str(), GetAvgCost(server.initialize), GetAvgCost(server.listen),
               GetAvgCost(server.server_match_endpoint), GetAvgCost(server.server_create_channel),
               GetAvgCost(server.server_export_mem), GetAvgCost(server.server_destroy_channel),
               GetAvgCost(server.server_cleanup_client), GetAvgCost(server.finalize));
  }
}

}  // namespace hixl
