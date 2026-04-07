/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/llm_log.h"
#include "llm_datadist_timer.h"
#include "statistic_manager.h"

namespace adxl {
namespace {
constexpr uint64_t kResetTimes = 100000UL;
constexpr uint64_t kFabricMemResetTimes = 10000UL;
constexpr uint32_t kStatisticTimerPeriodMs = 80U * 1000U;
constexpr uint64_t kZeroCost = 0UL;
}  // namespace

StatisticManager &StatisticManager::GetInstance() {
  (void)llm::LlmDatadistTimer::Instance();
  static StatisticManager instance;
  return instance;
}

void StatisticManager::StartPeriodicDumpIfNeeded() {
  std::lock_guard<std::mutex> lock(dump_mutex_);
  if (dump_timer_handle_ != nullptr) {
    return;
  }
  llm::LlmDatadistTimer::Instance().Init();
  dump_timer_handle_ = llm::LlmDatadistTimer::Instance().CreateTimer([this]() { Dump(); });
  (void)llm::LlmDatadistTimer::Instance().StartTimer(dump_timer_handle_, kStatisticTimerPeriodMs, false);
}

StatisticManager::~StatisticManager() {
  std::lock_guard<std::mutex> lock(dump_mutex_);
  if (dump_timer_handle_ != nullptr) {
    (void)llm::LlmDatadistTimer::Instance().StopTimer(dump_timer_handle_);
    (void)llm::LlmDatadistTimer::Instance().DeleteTimer(dump_timer_handle_);
    dump_timer_handle_ = nullptr;
  }
}

void StatisticManager::SetEnableUseFabricMem(bool enable_use_fabric_mem) {
  enable_use_fabric_mem_ = enable_use_fabric_mem;
}

void StatisticManager::RegisterChannel(const std::string &channel_id) {
  (void)GetOrCreateStatisticInfo(channel_id);
}

void StatisticManager::RemoveChannel(const std::string &channel_id) {
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  auto it = transfer_statistic_info_.find(channel_id);
  if (it != transfer_statistic_info_.end()) {
    transfer_statistic_info_.erase(it);
  }
}

void StatisticManager::UpdateCost(const uint64_t cost, std::atomic<uint64_t> &total_times,
                                  std::atomic<uint64_t> &max_cost, std::atomic<uint64_t> &total_cost) {
  (void)total_times.fetch_add(1U, std::memory_order_relaxed);
  (void)total_cost.fetch_add(cost, std::memory_order_relaxed);
  if (max_cost.load(std::memory_order_relaxed) < cost) {
    max_cost.store(cost, std::memory_order_relaxed);
  }
}

uint64_t StatisticManager::GetAvgCost(const CostStatisticInfo &cost_info) {
  const auto total_times = cost_info.times.load(std::memory_order_relaxed);
  if (total_times == 0U) {
    return 0U;
  }
  return cost_info.total_cost.load(std::memory_order_relaxed) / total_times;
}

uint64_t StatisticManager::GetOtherTotalCost(const ConnectStatisticInfo &cost_info) {
  const auto total_cost = cost_info.connect_total.total_cost.load(std::memory_order_relaxed);
  const auto tcp_cost = cost_info.tcp_connect.total_cost.load(std::memory_order_relaxed);
  const auto hccl_cost = cost_info.hccl_total.total_cost.load(std::memory_order_relaxed);
  if (total_cost <= tcp_cost + hccl_cost) {
    return kZeroCost;
  }
  return total_cost - tcp_cost - hccl_cost;
}

CostStatisticSnapshot StatisticManager::ToSnapshot(const CostStatisticInfo &cost_info) {
  return {cost_info.times.load(std::memory_order_relaxed), cost_info.max_cost.load(std::memory_order_relaxed),
          cost_info.total_cost.load(std::memory_order_relaxed)};
}

std::shared_ptr<StatisticInfo> StatisticManager::GetOrCreateStatisticInfo(const std::string &channel_id) {
  {
    std::shared_lock<std::shared_mutex> lock(map_mutex_);
    auto it = transfer_statistic_info_.find(channel_id);
    if (it != transfer_statistic_info_.end()) {
      return it->second;
    }
  }

  auto statistic_info = std::make_shared<StatisticInfo>();
  std::unique_lock<std::shared_mutex> lock(map_mutex_);
  auto [it, inserted] = transfer_statistic_info_.emplace(channel_id, statistic_info);
  return inserted ? statistic_info : it->second;
}

std::shared_ptr<StatisticInfo> StatisticManager::GetStatisticInfo(const std::string &channel_id) const {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  auto it = transfer_statistic_info_.find(channel_id);
  if (it == transfer_statistic_info_.end()) {
    return nullptr;
  }
  return it->second;
}

void StatisticManager::UpdateBufferTransferCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->buffer_transfer_statistic_info.transfer.times,
             info->buffer_transfer_statistic_info.transfer.max_cost,
             info->buffer_transfer_statistic_info.transfer.total_cost);
  if (info->buffer_transfer_statistic_info.transfer.times.load(std::memory_order_relaxed) > kResetTimes) {
    info->buffer_transfer_statistic_info.Reset();
  }
}

void StatisticManager::UpdateClientCopyCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->buffer_transfer_statistic_info.client_copy.times,
             info->buffer_transfer_statistic_info.client_copy.max_cost,
             info->buffer_transfer_statistic_info.client_copy.total_cost);
}

void StatisticManager::UpdateServerD2DCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->buffer_transfer_statistic_info.server_d2d.times,
             info->buffer_transfer_statistic_info.server_d2d.max_cost,
             info->buffer_transfer_statistic_info.server_d2d.total_cost);
}

void StatisticManager::UpdateServerCopyCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->buffer_transfer_statistic_info.server_copy.times,
             info->buffer_transfer_statistic_info.server_copy.max_cost,
             info->buffer_transfer_statistic_info.server_copy.total_cost);
  if (info->buffer_transfer_statistic_info.server_copy.times.load(std::memory_order_relaxed) > kResetTimes) {
    info->buffer_transfer_statistic_info.Reset();
  }
}

void StatisticManager::UpdateConnectTotalCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.connect_total.times, info->connect_statistic_info.connect_total.max_cost,
             info->connect_statistic_info.connect_total.total_cost);
}

void StatisticManager::UpdateTcpConnectCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.tcp_connect.times, info->connect_statistic_info.tcp_connect.max_cost,
             info->connect_statistic_info.tcp_connect.total_cost);
}

void StatisticManager::UpdateHcclTotalCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_total.times, info->connect_statistic_info.hccl_total.max_cost,
             info->connect_statistic_info.hccl_total.total_cost);
}

void StatisticManager::UpdateHcclCommInitCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_comm_init.times,
             info->connect_statistic_info.hccl_comm_init.max_cost,
             info->connect_statistic_info.hccl_comm_init.total_cost);
}

void StatisticManager::UpdateHcclCommBindMemCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_comm_bind_mem.times,
             info->connect_statistic_info.hccl_comm_bind_mem.max_cost,
             info->connect_statistic_info.hccl_comm_bind_mem.total_cost);
}

void StatisticManager::UpdateHcclCommPrepareCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->connect_statistic_info.hccl_comm_prepare.times,
             info->connect_statistic_info.hccl_comm_prepare.max_cost,
             info->connect_statistic_info.hccl_comm_prepare.total_cost);
}

void StatisticManager::UpdateFabricMemTransferCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->fabric_mem_transfer_statistic_info.transfer.times,
             info->fabric_mem_transfer_statistic_info.transfer.max_cost,
             info->fabric_mem_transfer_statistic_info.transfer.total_cost);
  if (info->fabric_mem_transfer_statistic_info.transfer.times.load(std::memory_order_relaxed) > kFabricMemResetTimes) {
    info->fabric_mem_transfer_statistic_info.Reset();
  }
}

void StatisticManager::UpdateFabricMemRealCopyCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->fabric_mem_transfer_statistic_info.real_copy.times,
             info->fabric_mem_transfer_statistic_info.real_copy.max_cost,
             info->fabric_mem_transfer_statistic_info.real_copy.total_cost);
}

void StatisticManager::UpdateDirectTransferCost(const std::string &channel_id, uint64_t cost) {
  auto info = GetOrCreateStatisticInfo(channel_id);
  UpdateCost(cost, info->direct_transfer_statistic_info.transfer.times,
             info->direct_transfer_statistic_info.transfer.max_cost,
             info->direct_transfer_statistic_info.transfer.total_cost);
  if (info->direct_transfer_statistic_info.transfer.times.load(std::memory_order_relaxed) > kResetTimes) {
    info->direct_transfer_statistic_info.Reset();
  }
}

StatisticInfoSnapshot StatisticManager::GetStatisticInfoSnapshot(const std::string &channel_id) const {
  auto info = GetStatisticInfo(channel_id);
  if (info == nullptr) {
    return {};
  }
  StatisticInfoSnapshot snapshot;
  snapshot.connect_statistic_info.connect_total = ToSnapshot(info->connect_statistic_info.connect_total);
  snapshot.connect_statistic_info.tcp_connect = ToSnapshot(info->connect_statistic_info.tcp_connect);
  snapshot.connect_statistic_info.hccl_total = ToSnapshot(info->connect_statistic_info.hccl_total);
  snapshot.connect_statistic_info.hccl_comm_init = ToSnapshot(info->connect_statistic_info.hccl_comm_init);
  snapshot.connect_statistic_info.hccl_comm_bind_mem = ToSnapshot(info->connect_statistic_info.hccl_comm_bind_mem);
  snapshot.connect_statistic_info.hccl_comm_prepare = ToSnapshot(info->connect_statistic_info.hccl_comm_prepare);
  return snapshot;
}

void StatisticManager::Dump() {
  DumpConnectStatisticInfo();
  if (enable_use_fabric_mem_) {
    DumpFabricMemTransferStatisticInfo();
  } else {
    DumpBufferTransferStatisticInfo();
    DumpDirectTransferStatisticInfo();
  }
}

void StatisticManager::DumpConnectStatisticInfo() {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : transfer_statistic_info_) {
    const auto &connect_info = item.second->connect_statistic_info;
    const auto connect_times = connect_info.connect_total.times.load(std::memory_order_relaxed);
    const auto other_total_cost = GetOtherTotalCost(connect_info);
    const auto other_avg_cost = connect_times == 0U ? 0U : other_total_cost / connect_times;
    LLMEVENT(
        "Connect statistic info[channel:%s, total times:%lu, max cost:%lu us, avg cost:%lu us, tcp times:%lu, "
        "max cost:%lu us, avg cost:%lu us, hccl times:%lu, max cost:%lu us, avg cost:%lu us, other avg cost:%lu us].",
        item.first.c_str(), connect_times, connect_info.connect_total.max_cost.load(std::memory_order_relaxed),
        GetAvgCost(connect_info.connect_total), connect_info.tcp_connect.times.load(std::memory_order_relaxed),
        connect_info.tcp_connect.max_cost.load(std::memory_order_relaxed), GetAvgCost(connect_info.tcp_connect),
        connect_info.hccl_total.times.load(std::memory_order_relaxed),
        connect_info.hccl_total.max_cost.load(std::memory_order_relaxed), GetAvgCost(connect_info.hccl_total),
        other_avg_cost);
    LLMEVENT(
        "Connect hccl detail[channel:%s, init times:%lu, max cost:%lu us, avg cost:%lu us, bind times:%lu, "
        "max cost:%lu us, avg cost:%lu us, prepare times:%lu, max cost:%lu us, avg cost:%lu us].",
        item.first.c_str(), connect_info.hccl_comm_init.times.load(std::memory_order_relaxed),
        connect_info.hccl_comm_init.max_cost.load(std::memory_order_relaxed), GetAvgCost(connect_info.hccl_comm_init),
        connect_info.hccl_comm_bind_mem.times.load(std::memory_order_relaxed),
        connect_info.hccl_comm_bind_mem.max_cost.load(std::memory_order_relaxed),
        GetAvgCost(connect_info.hccl_comm_bind_mem), connect_info.hccl_comm_prepare.times.load(std::memory_order_relaxed),
        connect_info.hccl_comm_prepare.max_cost.load(std::memory_order_relaxed),
        GetAvgCost(connect_info.hccl_comm_prepare));
  }
}

void StatisticManager::DumpBufferTransferStatisticInfo() {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : transfer_statistic_info_) {
    const auto &stat_info = item.second->buffer_transfer_statistic_info;
    LLMEVENT(
        "Buffer transfer statistic info[channel:%s, transfer times:%lu, max cost:%lu us, avg cost:%lu us, client copy "
        "times:%lu, max cost:%lu us, avg cost:%lu us, server comm times:%lu, max cost:%lu us, avg cost:%lu us, "
        "server copy times:%lu, max cost:%lu us, avg cost:%lu us].",
        item.first.c_str(), stat_info.transfer.times.load(std::memory_order_relaxed),
        stat_info.transfer.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.transfer),
        stat_info.client_copy.times.load(std::memory_order_relaxed),
        stat_info.client_copy.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.client_copy),
        stat_info.server_d2d.times.load(std::memory_order_relaxed),
        stat_info.server_d2d.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.server_d2d),
        stat_info.server_copy.times.load(std::memory_order_relaxed),
        stat_info.server_copy.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.server_copy));
  }
}

void StatisticManager::DumpFabricMemTransferStatisticInfo() {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : transfer_statistic_info_) {
    const auto &stat_info = item.second->fabric_mem_transfer_statistic_info;
    LLMEVENT(
        "Fabric mem transfer statistic info[channel:%s, transfer times:%lu, max cost:%lu us, avg cost:%lu us, real "
        "copy times:%lu, max cost:%lu us, avg cost:%lu us].",
        item.first.c_str(), stat_info.transfer.times.load(std::memory_order_relaxed),
        stat_info.transfer.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.transfer),
        stat_info.real_copy.times.load(std::memory_order_relaxed),
        stat_info.real_copy.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.real_copy));
  }
}

void StatisticManager::DumpDirectTransferStatisticInfo() {
  std::shared_lock<std::shared_mutex> lock(map_mutex_);
  for (const auto &item : transfer_statistic_info_) {
    const auto &stat_info = item.second->direct_transfer_statistic_info;
    LLMEVENT("Direct transfer statistic info[channel:%s, transfer times:%lu, max cost:%lu us, avg cost:%lu us].",
             item.first.c_str(), stat_info.transfer.times.load(std::memory_order_relaxed),
             stat_info.transfer.max_cost.load(std::memory_order_relaxed), GetAvgCost(stat_info.transfer));
  }
}

}  // namespace adxl
