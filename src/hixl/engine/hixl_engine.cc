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
#include "profiling/prof_api_reg.h"
#include "engine/local_comm_res_tool.h"
#include "acl/acl.h"

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

Status HixlEngine::Initialize(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("[HixlEngine] Initialization started, local_engine:%s", local_engine_.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHK_STATUS_RET(CheckOptions(options), "[HixlEngine] Failed to check options");
  std::string local_comm_res;
  Status ret = EndpointGenerator::BuildEndpointListFromOptions(
      options, local_engine_, local_comm_res, endpoint_list_);

  if (ret == PARAM_INVALID && endpoint_list_.empty()) {
    // fallback: 自动生成 local comm res
    HIXL_LOGI("[HixlEngine] No LocalCommRes in options, auto-generating from DCMI + topology");
    HIXL_CHK_STATUS_RET(GenerateLocalCommResFallback(options),
                        "[HixlEngine] Failed to auto-generate local comm res");
  } else {
    HIXL_CHK_STATUS_RET(ret, "[HixlEngine] Failed to build endpoint list from options");
  }

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

void HixlEngine::Finalize() {
  HIXL_LOGI("[HixlEngine] Finalization started");
  std::lock_guard<std::mutex> lock(mutex_);
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

Status HixlEngine::GenerateLocalCommResFallback(const std::map<AscendString, AscendString> &options) {
  // 1. 获取 phy_dev_id
  int32_t logic_id = 0;
  aclError acl_ret = aclrtGetDevice(&logic_id);
  HIXL_CHK_BOOL_RET_STATUS(acl_ret == ACL_SUCCESS, FAILED,
                           "[HixlEngine] aclrtGetDevice failed, ret=%d", static_cast<int>(acl_ret));
  int32_t phy_id = 0;
  acl_ret = aclrtGetPhyDevIdByLogicDevId(logic_id, &phy_id);
  HIXL_CHK_BOOL_RET_STATUS(acl_ret == ACL_SUCCESS, FAILED,
                           "[HixlEngine] aclrtGetPhyDevIdByLogicDevId failed, logic_id=%d, ret=%d",
                           logic_id, static_cast<int>(acl_ret));
  HIXL_LOGI("[HixlEngine] GenerateLocalCommResFallback: logic_id=%d, phy_id=%d", logic_id, phy_id);

  // 2. 从 options 或默认值读取 topo_path、route_path
  std::map<std::string, std::string> opts;
  auto find_option = [&options](const char *key) -> std::string {
    auto it = options.find(AscendString(key));
    if (it != options.end()) {
      return it->second.GetString();
    }
    return "";
  };

  std::string topo_path = find_option("topo_path");
  if (topo_path.empty()) {
    // 默认在 /etc/ 下匹配最新的 *noroce.json
    topo_path = "/etc/superpod_2d_noroce.json";
  }
  std::string route_path = find_option("route_path");
  if (route_path.empty()) {
    route_path = "/lib/route.conf";
  }
  std::string eid_json_path = find_option("eid_json_path");

  opts["topo_path"] = topo_path;
  opts["route_path"] = route_path;
  if (!eid_json_path.empty()) {
    opts["eid_json_path"] = eid_json_path;
  }
  HIXL_LOGI("[HixlEngine] GenerateLocalCommResFallback: topo_path=%s, route_path=%s",
            topo_path.c_str(), route_path.c_str());

  // 3. 调用 GenerateLocalCommRes
  hixl::LocalCommRes gen_res;
  int32_t ret = hixl::GenerateLocalCommRes(phy_id, opts, gen_res);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED,
                           "[HixlEngine] GenerateLocalCommRes failed, ret=%d", ret);

  // 4. GenerateLocalCommRes 已通过 DCMI 设置 net_instance_id，直接赋值
  endpoint_list_ = std::move(gen_res.endpoint_list);

  HIXL_LOGI("[HixlEngine] GenerateLocalCommResFallback: generated %zu endpoints, net_instance_id=%s",
            endpoint_list_.size(), gen_res.net_instance_id.c_str());
  return SUCCESS;
}
}  // namespace hixl
