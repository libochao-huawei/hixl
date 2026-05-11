/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <string>

#include "hixl_engine.h"
#include "engine/endpoint_generator.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/llm_utils.h"
#include "common/scope_guard.h"
#include "fabric_mem/virtual_memory_manager.h"
#include "profiling/prof_api_reg.h"

namespace hixl {

bool HixlEngine::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

Status HixlEngine::InitServer() {
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(local_engine_, ip, port),
                      "[HixlEngine] Failed to parse ip and port, local_engine should be in form as below: "
                      "ipv4 should be 'host_ip:host_port' or 'host_ip' "
                      "ipv6 should be '[host_ip]:host_port' or '[host_ip]' "
                      "current local_engine:%s",
                      local_engine_.c_str());
  HIXL_CHK_STATUS_RET(server_.Initialize(ip, port, endpoint_list_),
                      "[HixlEngine] Failed to initialize HixlEngine, local_engine:%s",
                      local_engine_.c_str());
  return SUCCESS;
}

bool HixlEngine::IsFabricMemMode() const {
  return fabric_mem_config_.enabled;
}

Status HixlEngine::InitFabricMem() {
  if (fabric_mem_config_.has_capacity_tb) {
    HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().SetVirtualMemoryCapacity(fabric_mem_config_.capacity_tb),
                        "[HixlEngine] Failed to set fabric memory capacity.");
  }
  if (fabric_mem_config_.has_start_address_tb) {
    HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().SetGlobalStartAddress(
                            fabric_mem_config_.start_address_tb),
                        "[HixlEngine] Failed to set fabric memory start address.");
  }
  HIXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().Initialize(),
                      "[HixlEngine] Failed to initialize fabric virtual memory manager.");
  fabric_mem_transfer_service_ = MakeUnique<FabricMemTransferService>();
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[HixlEngine] Failed to create fabric mem service.");
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->Initialize(fabric_mem_config_.max_stream_num,
                                                              fabric_mem_config_.task_stream_num,
                                                              &fabric_mem_statistic_),
                      "[HixlEngine] Failed to initialize fabric mem service.");
  HIXL_CHK_STATUS_RET(fabric_mem_statistic_.StartPeriodicDump(),
                      "[HixlEngine] Failed to start fabric mem statistic dump.");
  fabric_mem_control_server_ = MakeUnique<FabricMemControlServer>();
  HIXL_CHECK_NOTNULL(fabric_mem_control_server_.get(), "[HixlEngine] Failed to create fabric mem control server.");
  auto provider = [this](std::vector<ShareHandleInfo> &share_handles) -> Status {
    HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[HixlEngine] Fabric mem service is null.");
    share_handles = fabric_mem_transfer_service_->GetShareHandles();
    return SUCCESS;
  };
  HIXL_CHK_STATUS_RET(fabric_mem_control_server_->Start(local_engine_, provider),
                      "[HixlEngine] Failed to start fabric mem control server.");
  return SUCCESS;
}

Status HixlEngine::Initialize(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("[HixlEngine] Initialization started, local_engine:%s", local_engine_.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_STATUS_RET(CheckOptions(options), "[HixlEngine] Failed to check options");
  HIXL_CHK_STATUS_RET(FabricMemConfigParser::Parse(options, fabric_mem_config_),
                      "[HixlEngine] Failed to parse fabric mem config.");
  if (IsFabricMemMode()) {
    HIXL_CHK_STATUS_RET(InitFabricMem(), "[HixlEngine] Failed to initialize fabric mem mode.");
    is_initialized_ = true;
    HIXL_LOGI("[HixlEngine] FabricMem initialization succeeded, local_engine:%s", local_engine_.c_str());
    return SUCCESS;
  }
  std::string local_comm_res;
  HIXL_CHK_STATUS_RET(
      EndpointGenerator::BuildEndpointListFromOptions(options, local_engine_, local_comm_res, endpoint_list_),
      "[HixlEngine] Failed to build endpoint list from options");
  HIXL_CHK_STATUS_RET(ParseTrafficClass(options), "[HixlEngine] Failed to parse traffic class");
  HIXL_CHK_STATUS_RET(ParseServiceLevel(options), "[HixlEngine] Failed to parse service level");
  HIXL_CHK_STATUS_RET(InitServer(),
                      "[HixlEngine] Failed to initialize server, local_engine:%s, local_comm_res:%s",
                      local_engine_.c_str(), local_comm_res.c_str());
  is_initialized_ = true;
  HIXL_LOGI("[HixlEngine] Initialization succeeded, local_engine:%s", local_engine_.c_str());
  return SUCCESS;
}

Status HixlEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  if (IsFabricMemMode()) {
    return RegisterFabricMem(mem, type, mem_handle);
  }
  HIXL_LOGI("[HixlEngine] Registration started, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHK_STATUS_RET(server_.RegisterMem(mem, type, mem_handle),
                      "[HixlEngine] Failed to register mem, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
                      reinterpret_cast<void *>(mem.addr), mem.len);
  MemInfo mem_info = {mem_handle, mem, type};
  std::lock_guard<std::mutex> lock(mutex_);
  mem_map_.emplace(mem_handle, mem_info);
  HIXL_LOGI("[HixlEngine] Registration succeeded, type:%s, addr:%p, size:%lu", MemTypeToString(type).c_str(),
            reinterpret_cast<void *>(mem.addr), mem.len);
  return SUCCESS;
}

Status HixlEngine::DeregisterMem(MemHandle mem_handle) {
  if (IsFabricMemMode()) {
    return DeregisterFabricMem(mem_handle);
  }
  HIXL_LOGI("[HixlEngine] Deregistration started, mem_handle: %p", mem_handle);
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = mem_map_.find(mem_handle);
  if (it == mem_map_.end()) {
    HIXL_LOGW("[HixlEngine] handle:%p is not registered", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(
      client_manager_.IsEmpty(), FAILED,
      "[HixlEngine] Failed to deregister mem. All clients must be disconnected before deregistration, "
      "mem_handle: %p, local_engine: %s",
      mem_handle, local_engine_.c_str());
  HIXL_CHK_STATUS_RET(server_.DeregisterMem(mem_handle),
                      "[HixlEngine] Failed to deregister mem, mem_handle: %p, local_engine: %s", mem_handle,
                      local_engine_.c_str());
  mem_map_.erase(it);
  HIXL_LOGI("[HixlEngine] Deregistration succeeded, mem_handle: %p", mem_handle);
  return SUCCESS;
}

Status HixlEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  if (IsFabricMemMode()) {
    return ConnectFabricMem(remote_engine, timeout_in_millis);
  }
  HIXL_CHK_BOOL_RET_STATUS(strcmp(local_engine_.c_str(), remote_engine.GetString()) != 0, PARAM_INVALID,
                           "[HixlEngine] Do not support connection with self, please check remote engine. "
                           "local_engine:%s, remote_engine:%s",
                           local_engine_.c_str(), remote_engine.GetString());
  HIXL_LOGI("[HixlEngine] Connection started, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  ClientPtr client_ptr = nullptr;
  client_ptr = client_manager_.GetClient(remote_engine.GetString());
  if (client_ptr != nullptr) {
    HIXL_LOGE(ALREADY_CONNECTED, "[HixlEngine] remote engine:%s is already connected to local_engine:%s",
              remote_engine.GetString(), local_engine_.c_str());
    return ALREADY_CONNECTED;
  }
  ClientConfig config{};
  config.endpoint_list = endpoint_list_;
  config.remote_engine = remote_engine.GetString();
  config.rdma_tc = rdma_traffic_class_;
  config.rdma_sl = rdma_service_level_;
  HIXL_CHK_STATUS_RET(client_manager_.CreateClient(config, client_ptr),
                      "[HixlEngine] Failed to create HixlClient, local_engine: %s, remote engine: %s",
                      local_engine_.c_str(), remote_engine.GetString());
  HIXL_CHECK_NOTNULL(
      client_ptr,
      "[HixlEngine] Created client is null, please check your parameters! local_engine:%s, remote_engine:%s",
      local_engine_.c_str(), remote_engine.GetString());
  std::vector<MemInfo> mem_info_list;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &pair : mem_map_) {
    mem_info_list.push_back(pair.second);
  }
  HIXL_DISMISSABLE_GUARD(rollback,
                         ([this, &remote_engine]() { client_manager_.DestroyClient(remote_engine.GetString()); }));
  HIXL_CHK_STATUS_RET(client_ptr->SetLocalMemInfo(mem_info_list),
                      "[HixlEngine] Failed to set local memory info, local_engine:%s", local_engine_.c_str());
  HIXL_CHK_STATUS_RET(client_ptr->Connect(timeout_in_millis),
                      "[HixlEngine] Failed to connect, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_DISMISS_GUARD(rollback);
  HIXL_LOGI("[HixlEngine] Connection succeeded, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  if (IsFabricMemMode()) {
    (void)timeout_in_millis;
    return DisconnectFabricMem(remote_engine);
  }
  HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_STATUS_RET(client_manager_.DestroyClient(remote_engine.GetString()),
                      "[HixlEngine] Failed to disconnect, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("[HixlEngine] Disconnection succeeded, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

void HixlEngine::Disconnect() {
  if (IsFabricMemMode()) {
    DisconnectFabricMem();
    return;
  }
  HIXL_LOGI("[HixlEngine] Disconnection with all clients started, local_engine:%s", local_engine_.c_str());
  Status ret = client_manager_.Finalize();
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED, "[HixlEngine] Failed to disconnect with all clients, local_engine:%s", local_engine_.c_str());
  } else {
    HIXL_LOGI("[HixlEngine] Disconnection with all clients succeeded, local_engine:%s", local_engine_.c_str());
  }
}

Status HixlEngine::TransferSync(const AscendString &remote_engine, TransferOp operation,
                                const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  if (IsFabricMemMode()) {
    return TransferSyncFabricMem(remote_engine, operation, op_descs, timeout_in_millis);
  }
  HIXL_LOGI("[HixlEngine] Synchronous transmission started, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(client_ptr != nullptr, NOT_CONNECTED,
                           "[HixlEngine] Failed to get client through remote engine, please check connection. "
                           "local_engine:%s, remote_engine:%s",
                           local_engine_.c_str(), remote_engine.GetString());
  HixlProfType type = (operation == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
  HIXL_API_PROFILING(type);
  HIXL_CHK_STATUS_RET(client_ptr->TransferSync(op_descs, operation, timeout_in_millis),
                      "[HixlEngine] Failed to TransferSync, local_engine:%s, remote_engine:%s, timeout:%d ms",
                      local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("[HixlEngine] Synchronous transmission succeeded, local_engine:%s, remote_engine:%s, timeout:%d ms",
            local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status HixlEngine::TransferAsync(const AscendString &remote_engine, TransferOp operation,
                                 const std::vector<TransferOpDesc> &op_descs, const TransferArgs &optional_args,
                                 TransferReq &req) {
  if (IsFabricMemMode()) {
    (void)optional_args;
    return TransferAsyncFabricMem(remote_engine, operation, op_descs, req);
  }
  HIXL_LOGI("[HixlEngine] Asynchronous transmission started, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
            remote_engine.GetString());
  (void)optional_args;
  ClientPtr client_ptr = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(
      client_ptr != nullptr, NOT_CONNECTED,
      "[HixlEngine] Failed to get client through remote engine, please check connection. remote_engine:%s",
      remote_engine.GetString());
  HIXL_CHK_STATUS_RET(client_ptr->TransferAsync(op_descs, operation, req),
                      "[HixlEngine] Failed to TransferAsync, local_engine:%s, remote_engine:%s", local_engine_.c_str(),
                      remote_engine.GetString());
  auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  uint64_t start_time = 0;
  start_time = HixlProfilingReporter::GetSysCycleTime();
  TransferInfo transfer_info = {start_time, operation, remote_engine};
  std::lock_guard<std::mutex> lock(mutex_);
  req_map_.emplace(id, transfer_info);
  HIXL_LOGI("[HixlEngine] Asynchronous transmission succeeded, local_engine:%s, remote_engine:%s",
            local_engine_.c_str(), remote_engine.GetString());
  return SUCCESS;
}

Status HixlEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  if (IsFabricMemMode()) {
    return GetTransferStatusFabricMem(req, status);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  auto it = req_map_.find(id);
  if (it == req_map_.cend()) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(PARAM_INVALID, "[HixlEngine] Request not found, request has been completed or does not exist, req: %p",
              req);
    return PARAM_INVALID;
  }
  auto remote_engine = it->second.remote_engine;
  auto client = client_manager_.GetClient(remote_engine.GetString());
  HIXL_CHECK_NOTNULL(client,
                     "[HixlEngine] Failed to get client through remote engine, local_engine:%s, remote_engine:%s",
                     local_engine_.c_str(), remote_engine.GetString());
  HIXL_CHK_STATUS_RET(client->GetTransferStatus(req, status), 
                      "[HixlEngine] Failed to get status through client, req:%p, status:%d", 
                      req, static_cast<int>(status));
  if (status == TransferStatus::COMPLETED) {
    auto op_type = it->second.op_type;
    auto start_time = it->second.start_time;
    HixlProfType type = (op_type == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
    HIXL_API_PROFILING_WITH_TIME(type, start_time);
    req_map_.erase(it);
  }
  return SUCCESS;
}

bool HixlEngine::HasFabricMemConnectionsLocked() const {
  return !fabric_mem_remote_mems_.empty();
}

Status HixlEngine::RegisterFabricMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_LOGI("[HixlEngine] FabricMem registration started, type:%s, addr:%p, size:%lu",
            MemTypeToString(type).c_str(), reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[HixlEngine] FabricMem service is null.");
  AddrInfo cur_info{mem.addr, mem.addr + mem.len, type};
  std::lock_guard<std::mutex> lock(mutex_);
  std::map<MemHandle, AddrInfo> addr_map;
  for (const auto &item : mem_map_) {
    addr_map[item.first] = {item.second.mem.addr, item.second.mem.addr + item.second.mem.len, item.second.type};
  }
  bool is_duplicate = false;
  MemHandle existing_handle = nullptr;
  HIXL_CHK_STATUS_RET(CheckAddrOverlap(cur_info, addr_map, is_duplicate, existing_handle),
                      "[HixlEngine] Failed to check FabricMem address overlap.");
  if (is_duplicate) {
    mem_handle = existing_handle;
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->RegisterMem(mem, type, mem_handle),
                      "[HixlEngine] Failed to register FabricMem.");
  mem_map_.emplace(mem_handle, MemInfo{mem_handle, mem, type});
  HIXL_LOGI("[HixlEngine] FabricMem registration succeeded, handle:%p.", mem_handle);
  return SUCCESS;
}

Status HixlEngine::DeregisterFabricMem(MemHandle mem_handle) {
  HIXL_LOGI("[HixlEngine] FabricMem deregistration started, handle:%p.", mem_handle);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[HixlEngine] FabricMem service is null.");
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = mem_map_.find(mem_handle);
  if (it == mem_map_.end()) {
    HIXL_LOGW("[HixlEngine] FabricMem handle:%p is not registered.", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(!HasFabricMemConnectionsLocked(), FAILED,
                           "[HixlEngine] Disconnect FabricMem peers before deregistering memory.");
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->DeregisterMem(mem_handle),
                      "[HixlEngine] Failed to deregister FabricMem.");
  mem_map_.erase(it);
  return SUCCESS;
}

Status HixlEngine::ConnectFabricMem(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(strcmp(local_engine_.c_str(), remote_engine.GetString()) != 0, PARAM_INVALID,
                           "[HixlEngine] FabricMem does not support connection with self, local_engine:%s.",
                           local_engine_.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string remote = remote_engine.GetString();
  HIXL_CHK_BOOL_RET_STATUS(fabric_mem_remote_mems_.find(remote) == fabric_mem_remote_mems_.end(), ALREADY_CONNECTED,
                           "[HixlEngine] FabricMem remote engine:%s is already connected.", remote.c_str());
  std::vector<ShareHandleInfo> share_handles;
  HIXL_CHK_STATUS_RET(FabricMemControlClient::Fetch(remote, timeout_in_millis, share_handles),
                      "[HixlEngine] Failed to fetch FabricMem share handles from remote:%s.", remote.c_str());
  auto remote_memory = MakeUnique<FabricMemRemoteMemory>();
  HIXL_CHECK_NOTNULL(remote_memory.get(), "[HixlEngine] Failed to create FabricMem remote memory.");
  int32_t device_id = -1;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id), "[HixlEngine] Failed to get device id.");
  HIXL_CHK_STATUS_RET(remote_memory->Import(share_handles, device_id),
                      "[HixlEngine] Failed to import remote FabricMem, remote:%s.", remote.c_str());
  fabric_mem_statistic_.RegisterChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  fabric_mem_remote_mems_[remote] = std::move(remote_memory);
  HIXL_EVENT("[HixlEngine] FabricMem connected, local_engine:%s, remote_engine:%s.",
             local_engine_.c_str(), remote.c_str());
  return SUCCESS;
}

Status HixlEngine::DisconnectFabricMem(const AscendString &remote_engine) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string remote = remote_engine.GetString();
  const auto it = fabric_mem_remote_mems_.find(remote);
  if (it == fabric_mem_remote_mems_.end()) {
    return NOT_CONNECTED;
  }
  if (fabric_mem_transfer_service_ != nullptr) {
    fabric_mem_transfer_service_->RemoveChannel(remote);
  }
  fabric_mem_statistic_.RemoveStatisticChannel(FabricMemStatistic::GetClientStatisticChannelId(remote));
  fabric_mem_remote_mems_.erase(it);
  return SUCCESS;
}

void HixlEngine::DisconnectFabricMem() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fabric_mem_transfer_service_ != nullptr) {
    for (const auto &item : fabric_mem_remote_mems_) {
      fabric_mem_transfer_service_->RemoveChannel(item.first);
      fabric_mem_statistic_.RemoveStatisticChannel(FabricMemStatistic::GetClientStatisticChannelId(item.first));
    }
  }
  fabric_mem_remote_mems_.clear();
}

Status HixlEngine::BuildFabricMemContextLocked(const std::string &remote_engine, FabricMemTransferContext &context) {
  const auto it = fabric_mem_remote_mems_.find(remote_engine);
  HIXL_CHK_BOOL_RET_STATUS(it != fabric_mem_remote_mems_.end(), NOT_CONNECTED,
                           "[HixlEngine] FabricMem remote engine:%s is not connected.", remote_engine.c_str());
  context.channel_id = remote_engine;
  context.statistic_channel_id = FabricMemStatistic::GetClientStatisticChannelId(remote_engine);
  context.remote_va_to_old_va = it->second->GetNewVaToOldVa();
  return SUCCESS;
}

Status HixlEngine::TransferSyncFabricMem(const AscendString &remote_engine, TransferOp operation,
                                         const std::vector<TransferOpDesc> &op_descs,
                                         int32_t timeout_in_millis) {
  HixlProfType type = (operation == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
  HIXL_API_PROFILING(type);
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[HixlEngine] FabricMem service is null.");
  FabricMemTransferContext context;
  HIXL_CHK_STATUS_RET(BuildFabricMemContextLocked(remote_engine.GetString(), context),
                      "[HixlEngine] Failed to build FabricMem transfer context.");
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->Transfer(context, operation, op_descs, timeout_in_millis),
                      "[HixlEngine] FabricMem TransferSync failed.");
  return SUCCESS;
}

Status HixlEngine::TransferAsyncFabricMem(const AscendString &remote_engine, TransferOp operation,
                                          const std::vector<TransferOpDesc> &op_descs, TransferReq &req) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[HixlEngine] FabricMem service is null.");
  FabricMemTransferContext context;
  HIXL_CHK_STATUS_RET(BuildFabricMemContextLocked(remote_engine.GetString(), context),
                      "[HixlEngine] Failed to build FabricMem transfer context.");
  const uint64_t id = next_fabric_req_id_.fetch_add(1U, std::memory_order_relaxed);
  req = reinterpret_cast<void *>(static_cast<uintptr_t>(id));
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->TransferAsync(context, operation, op_descs, req),
                      "[HixlEngine] FabricMem TransferAsync failed.");
  const uint64_t start_time = HixlProfilingReporter::GetSysCycleTime();
  req_map_.emplace(id, TransferInfo{start_time, operation, remote_engine});
  return SUCCESS;
}

Status HixlEngine::GetTransferStatusFabricMem(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(req));
  const auto it = req_map_.find(id);
  HIXL_CHK_BOOL_RET_STATUS(it != req_map_.end(), PARAM_INVALID,
                           "[HixlEngine] FabricMem request:%p not found.", req);
  HIXL_CHECK_NOTNULL(fabric_mem_transfer_service_.get(), "[HixlEngine] FabricMem service is null.");
  FabricMemTransferContext context;
  HIXL_CHK_STATUS_RET(BuildFabricMemContextLocked(it->second.remote_engine.GetString(), context),
                      "[HixlEngine] Failed to build FabricMem transfer context.");
  HIXL_CHK_STATUS_RET(fabric_mem_transfer_service_->GetTransferStatus(context, req, status),
                      "[HixlEngine] Failed to get FabricMem transfer status.");
  if (status != TransferStatus::WAITING) {
    if (status == TransferStatus::COMPLETED) {
      const auto type = (it->second.op_type == READ ? HixlProfType::HixlOpBatchRead : HixlProfType::HixlOpBatchWrite);
      HIXL_API_PROFILING_WITH_TIME(type, it->second.start_time);
    }
    req_map_.erase(it);
  }
  return SUCCESS;
}

void HixlEngine::Finalize() {
  HIXL_LOGI("[HixlEngine] Finalization started");
  std::lock_guard<std::mutex> lock(mutex_);
  if (IsFabricMemMode()) {
    if (fabric_mem_control_server_ != nullptr) {
      fabric_mem_control_server_->Stop();
    }
    fabric_mem_remote_mems_.clear();
    if (fabric_mem_transfer_service_ != nullptr) {
      fabric_mem_transfer_service_->Finalize();
    }
    fabric_mem_statistic_.StopPeriodicDump();
    mem_map_.clear();
    req_map_.clear();
    is_initialized_ = false;
    HIXL_LOGI("[HixlEngine] FabricMem finalization succeeded");
    return;
  }
  server_.Finalize();
  client_manager_.Finalize();
  mem_map_.clear();
  req_map_.clear();
  is_initialized_ = false;
  HIXL_LOGI("[HixlEngine] Finalization succeeded");
}

Status HixlEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  (void)remote_engine;
  (void)notify;
  (void)timeout_in_millis;
  HIXL_LOGE(UNSUPPORTED, "[HixlEngine] Method SendNotify is not supported by HixlEngine yet");
  return UNSUPPORTED;
}

Status HixlEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  (void)notifies;
  HIXL_LOGE(UNSUPPORTED, "[HixlEngine] Method GetNotifies is not supported by HixlEngine yet");
  return UNSUPPORTED;
}

Status HixlEngine::RegisterCallbackProcessor(int32_t msg_type, CallbackProcessor processor) {
  if (IsFabricMemMode()) {
    HIXL_LOGE(UNSUPPORTED, "[HixlEngine] Method RegisterCallbackProcessor is not supported in FabricMem mode.");
    return UNSUPPORTED;
  }
  HIXL_CHK_STATUS_RET(server_.RegisterCallbackProcessor(msg_type, processor),
                      "[HixlEngine] Failed to register msg callback, msg type:%d", msg_type);
  return SUCCESS;
}

Status HixlEngine::ParseTrafficClass(const std::map<AscendString, AscendString> &options) {
  std::string traffic_class_str;
  const auto &traffic_it = options.find(hixl::OPTION_RDMA_TRAFFIC_CLASS);
  const auto &traffic_it2 = options.find(adxl::OPTION_RDMA_TRAFFIC_CLASS);
  auto it = traffic_it == options.cend() ? traffic_it2 : traffic_it;
  if (it != options.cend()) {
    traffic_class_str = it->second.GetString();
  }
  // 若options未设置 traffic_class 则检查环境变量 HCCL_RDMA_TC 是否设置
  if (traffic_class_str.empty()) {
    std::string env_tc;
    const char *env_ret = std::getenv("HCCL_RDMA_TC");
    if (env_ret != nullptr) {
      env_tc = env_ret;
    }
    traffic_class_str = env_tc;
  }
  if (!traffic_class_str.empty()) {
    int32_t traffic_class = 0;
    HIXL_CHK_STATUS_RET(ToNumber(traffic_class_str, traffic_class), "Traffic class is invalid, value = %s",
                        traffic_class_str.c_str());
    HIXL_CHK_BOOL_RET_STATUS(traffic_class >= 0 && traffic_class <= 255 && (traffic_class % 4 == 0), PARAM_INVALID,
                             "Traffic class is invalid, value = %d, must be between 0-255 and a multiple of 4",
                             traffic_class);
    rdma_traffic_class_ = static_cast<uint8_t>(traffic_class);
    HIXL_LOGI("Set rdma traffic class to %d.", traffic_class);
  }
  return SUCCESS;
}

Status HixlEngine::ParseServiceLevel(const std::map<AscendString, AscendString> &options) {
  std::string service_level_str;
  const auto &service_it = options.find(hixl::OPTION_RDMA_SERVICE_LEVEL);
  const auto &service_it2 = options.find(adxl::OPTION_RDMA_SERVICE_LEVEL);
  auto it = service_it == options.cend() ? service_it2 : service_it;
  if (it != options.cend()) {
    service_level_str = it->second.GetString();
  }
  // 若options未设置 service_level 则检查环境变量 HCCL_RDMA_SL 是否设置
  if (service_level_str.empty()) {
    std::string env_sl;
    const char *env_ret = std::getenv("HCCL_RDMA_SL");
    if (env_ret != nullptr) {
      env_sl = env_ret;
    }
    service_level_str = env_sl;
  }
  if (!service_level_str.empty()) {
    int32_t service_level = 0;
    HIXL_CHK_STATUS_RET(ToNumber(service_level_str, service_level), "Service level is invalid, value = %s",
                        service_level_str.c_str());
    HIXL_CHK_BOOL_RET_STATUS(service_level >= 0 && service_level <= 7, PARAM_INVALID,
                             "service_level must be in [0, 7], value = %d",
                             service_level);
    rdma_service_level_ = static_cast<uint8_t>(service_level);
    HIXL_LOGI("Set rdma service level to %d.", service_level);
  }
  return SUCCESS;
}
}  // namespace hixl
