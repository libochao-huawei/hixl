/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_client.h"
#include "cs/hixl_cs_client.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>
#include <utility>
#include <cstdlib>
#include <thread>
#include "securec.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "engine/endpoint_generator.h"
#include "common/hixl_utils.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"
#include "common/thread_pool.h"
#include "engine/client_handler_factory.h"

namespace hixl {
namespace {
constexpr uint64_t kMaxRecvRespBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);
constexpr uint32_t kCtrlMsgPluginTimeoutMs = 10000U;
constexpr const char *kMemTypeDevice = "DEVICE";
constexpr const char *kMemTypeHost = "HOST";

const char *CommTypeToString(CommType type) {
  switch (type) {
    case CommType::COMM_TYPE_UB_D2D:
      return "UB_D2D";
    case CommType::COMM_TYPE_UB_H2D:
      return "UB_H2D";
    case CommType::COMM_TYPE_UB_D2H:
      return "UB_D2H";
    case CommType::COMM_TYPE_UB_H2H:
      return "UB_H2H";
    case CommType::COMM_TYPE_ROCE:
      return "ROCE";
    case CommType::COMM_TYPE_HCCS:
      return "HCCS";
    case CommType::COMM_TYPE_UBOE:
      return "UBOE";
    default:
      return "UNKNOWN";
  }
}

Status ComputeBatchSyncRemainingMs(const std::chrono::steady_clock::time_point &sync_start, uint32_t timeout_ms,
                                   uint32_t &remaining_ms) {
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - sync_start)
                              .count();
  if (elapsed_ms >= static_cast<int64_t>(timeout_ms)) {
    HIXL_LOGE(TIMEOUT, "HixlClient BatchTransferSync no time left after %lld ms of %u ms budget",
              static_cast<long long>(elapsed_ms), timeout_ms);
    return TIMEOUT;
  }
  remaining_ms = static_cast<uint32_t>(static_cast<int64_t>(timeout_ms) - std::max<int64_t>(0L, elapsed_ms));
  return SUCCESS;
}

Status BatchTransferSyncInvoke(HixlClientHandle handle, CommType type,
                               const std::vector<TransferOpDesc> &op_descs_vec, TransferOp operation,
                               uint32_t remaining_timeout_ms) {
  const auto start = std::chrono::steady_clock::now();
  HIXL_LOGI("HixlClient BatchTransferSync start, type:%s, op_descs size:%zu, remaining_timeout_ms:%u",
            CommTypeToString(type), op_descs_vec.size(), remaining_timeout_ms);
  const uint32_t list_num = static_cast<uint32_t>(op_descs_vec.size());
  std::vector<void *> remote_bufs(list_num);
  std::vector<void *> local_bufs(list_num);
  std::vector<const void *> local_const_bufs(list_num);
  std::vector<const void *> remote_const_bufs(list_num);
  std::vector<uint64_t> lens(list_num);
  for (size_t i = 0; i < list_num; i++) {
    remote_bufs[i] = reinterpret_cast<void *>(op_descs_vec[i].remote_addr);
    local_bufs[i] = reinterpret_cast<void *>(op_descs_vec[i].local_addr);
    local_const_bufs[i] = reinterpret_cast<const void *>(op_descs_vec[i].local_addr);
    remote_const_bufs[i] = reinterpret_cast<const void *>(op_descs_vec[i].remote_addr);
    lens[i] = op_descs_vec[i].len;
  }
  const auto prepare_end = std::chrono::steady_clock::now();
  const auto prepare_us = std::chrono::duration_cast<std::chrono::microseconds>(prepare_end - start).count();

  CommunicateMem com_mem{};
  com_mem.list_num = list_num;
  com_mem.len_list = lens.data();
  auto *cs_client = static_cast<HixlCSClient *>(handle);
  if (operation == WRITE) {
    com_mem.dst_buf_list = remote_bufs.data();
    com_mem.src_buf_list = local_const_bufs.data();
    HIXL_CHK_STATUS_RET(cs_client->BatchTransferSync(false, com_mem, remaining_timeout_ms),
                        "HixlClient BatchPutSync failed, client_handle: %p", handle);
  } else {
    com_mem.dst_buf_list = local_bufs.data();
    com_mem.src_buf_list = remote_const_bufs.data();
    HIXL_CHK_STATUS_RET(cs_client->BatchTransferSync(true, com_mem, remaining_timeout_ms),
                        "HixlClient BatchGetSync failed, client_handle: %p", handle);
  }
  const auto transfer_end = std::chrono::steady_clock::now();
  const auto transfer_us = std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - prepare_end).count();
  const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - start).count();
  HIXL_LOGE(SUCCESS, "[HixlClient] BatchTransferSyncInvoke timing(us): total=%ld prepare=%ld transfer=%ld type=%s list_num=%u op=%s",
            total_us, prepare_us, transfer_us, CommTypeToString(type), list_num,
            (operation == WRITE) ? "WRITE" : "READ");
  return SUCCESS;
}
}  // namespace

Status HixlClient::Initialize(const std::vector<EndpointConfig> &local_endpoint_list) {
  if (local_endpoint_list.empty()) {
    HIXL_LOGE(PARAM_INVALID, "The input local_endpoint_list is empty");
    return PARAM_INVALID;
  }
  std::vector<EndpointConfig> remote_endpoint_list;
  CtrlMsgPlugin::Initialize();
  {
    int32_t socket = -1;
    HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, socket, kCtrlMsgPluginTimeoutMs),
                        "Connect socket failed");
    ScopeGuard socket_guard([&socket]() {
      if (socket != -1) {
        HIXL_LOGI("HixlClient close socket start, socket:%d", socket);
        close(socket);
        HIXL_LOGI("HixlClient close socket end, socket:%d", socket);
        socket = -1;
      }
    });
    HIXL_CHK_STATUS_RET(SendEndpointInfoReq(socket, CtrlMsgType::kGetEndpointInfoReq),
                        "HixlClient send GetEndpointInfoReq failed, socket:%d", socket);
    HIXL_CHK_STATUS_RET(RecvEndpointInfoResp(socket, remote_endpoint_list),
                        "HixlClient receive GetEndpointInfoResp failed, socket:%d", socket);
  }
  if (remote_endpoint_list.empty()) {
    HIXL_LOGE(FAILED, "HixlClient received empty remote_endpoint_list");
    return FAILED;
  }
  client_handler_ = ClientHandlerFactory::Create(server_ip_, server_port_, rdma_tc_, rdma_sl_,
                                                   local_endpoint_list, remote_endpoint_list);
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "ClientHandlerFactory failed to create client handler");
    return FAILED;
  }
  return SUCCESS;
}

Status HixlClient::SendEndpointInfoReq(int32_t fd, CtrlMsgType msg_type) const {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &header, static_cast<uint64_t>(sizeof(header))),
                      "HixlClient send header failed, fd:%d", fd);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))),
                      "HixlClient send msg_type failed, fd:%d", fd);
  return SUCCESS;
}

Status HixlClient::RecvEndpointInfoResp(int32_t fd, std::vector<EndpointConfig> &remote_endpoint_list) const {
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &header, static_cast<uint32_t>(sizeof(header)), kCtrlMsgPluginTimeoutMs),
                      "HixlClient receive header failed, fd:%d", fd);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID,
                           "Invalid magic for HixlClient RecvEndpointInfoResp, expect:0x%X, actual:0x%X", kMagicNumber,
                           header.magic);
  HIXL_CHK_BOOL_RET_STATUS(
      header.body_size > sizeof(CtrlMsgType) && header.body_size <= kMaxRecvRespBodySize, PARAM_INVALID,
      "Invalid body_size in HixlClient RecvEndpointInfoResp, body_size=%" PRIu64 ", must be in (%zu, %" PRIu64 "]",
      header.body_size, sizeof(CtrlMsgType), kMaxRecvRespBodySize);

  const uint64_t body_size = header.body_size;
  std::vector<uint8_t> body(body_size);
  HIXL_EVENT("[HixlClient] RecvEndpointInfoResp: receiving remote_endpoint_list body (%" PRIu64
             " bytes) from fd=%d begin",
             body_size, fd);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, body.data(), static_cast<uint32_t>(body_size), kCtrlMsgPluginTimeoutMs));
  HIXL_EVENT("[HixlClient] RecvEndpointInfoResp: receiving remote_endpoint_list body (%" PRIu64
             " bytes) from fd=%d success",
             body_size, fd);

  CtrlMsgType msg_type{};
  const void *src = static_cast<const void *>(body.data());
  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s msg_type failed, rc=%d", static_cast<int32_t>(rc));
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kGetEndpointInfoResp, PARAM_INVALID,
                           "Unexpected msg_type=%d in RecvEndpointInfoResp, expect=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(CtrlMsgType::kGetEndpointInfoResp));

  const size_t json_len = static_cast<size_t>(body_size - sizeof(CtrlMsgType));
  std::string json_str(reinterpret_cast<const char *>(body.data() + sizeof(msg_type)), json_len);
  return EndpointGenerator::DeserializeEndpointConfigList(json_str, remote_endpoint_list);
}

Status HixlClient::SetLocalMemInfo(const std::vector<MemInfo> &mem_info_list) {
  HIXL_LOGI("SetLocalMemInfo begin");
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    return FAILED;
  }
  {
    std::lock_guard<std::mutex> lock(client_handler_->GetHandleMutex());
    if (client_handler_->GetHandles().empty()) {
      HIXL_LOGE(FAILED, "HixlClient is not initialized");
      return FAILED;
    }
  }
  for (const auto &mem_info : mem_info_list) {
    HIXL_CHK_STATUS_RET(client_handler_->ProcessLocalMem(mem_info, server_ip_, server_port_, rdma_tc_, rdma_sl_),
                        "Failed to process local memory, addr: 0x%lx, size: %lu, type: %s", mem_info.mem.addr,
                        mem_info.mem.len, (mem_info.type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
  }
  HIXL_LOGI("SetLocalMemInfo end");
  return SUCCESS;
}

Status HixlClient::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(client_handler_->GetHandleMutex());
  if (client_handler_->GetHandles().empty()) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    return FAILED;
  }

  HIXL_LOGI("HixlClient connect start, timeout:%u ms", timeout_ms);
  ThreadPool thread_pool("hixl_client_connect", client_handler_->GetHandles().size());
  std::vector<std::future<Status>> connect_futures;
  aclrtContext context = nullptr;
  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&context));
  HIXL_LOGI("HixlClient aclrtGetCurrentContext, context: %p", context);
  for (const auto &pair : client_handler_->GetHandles()) {
    auto type = pair.first;
    auto handle = pair.second;
    auto future = thread_pool.commit([handle, timeout_ms, type, context]() -> Status {
      HIXL_CHK_ACL_RET(aclrtSetCurrentContext(context));
      HIXL_LOGI("HixlClient aclrtSetCurrentContext, context: %p", context);
      try {
        HIXL_CHK_STATUS_RET(HixlCSClientConnect(handle, timeout_ms),
                            "HixlClient Connect failed for type:%s, client_handle: %p, timeout:%u ms",
                            CommTypeToString(type), handle, timeout_ms);
        return SUCCESS;
      } catch (const std::exception &e) {
        HIXL_LOGE(FAILED, "Exception in HixlCSClientConnectSync: %s", e.what());
        return FAILED;
      } catch (...) {
        HIXL_LOGE(FAILED, "Unknown exception in HixlCSClientConnectSync");
        return FAILED;
      }
    });
    connect_futures.emplace_back(std::move(future));
  }
  for (auto &future : connect_futures) {
    HIXL_CHK_STATUS_RET(future.get(), "HixlClient Connect failed, timeout:%u ms", timeout_ms);
  }
  HIXL_LOGI("HixlClient Connect success, timeout:%u ms", timeout_ms);

  HIXL_CHK_STATUS_RET(ProcessRemoteMem(timeout_ms), "HixlClient ProcessRemoteMem failed, timeout:%u ms", timeout_ms);
  std::lock_guard<std::mutex> status_lock(status_mutex_);
  is_connected_ = true;
  return SUCCESS;
}

Status HixlClient::ProcessRemoteMem(uint32_t timeout_ms) {
  HIXL_LOGI("ProcessRemoteMem begin");
  for (const auto &pair : client_handler_->GetHandles()) {
    auto handle = pair.second;
    CommMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    uint32_t list_num = 0;
    HIXL_CHK_STATUS_RET(HixlCSClientGetRemoteMem(handle, &remote_mem_list, &mem_tag_list, &list_num, timeout_ms),
                        "HixlClient get remote memories failed, client_handle: %p, timeout:%u ms", handle, timeout_ms);
    HIXL_CHK_STATUS_RET(client_handler_->AddRemoteMem(remote_mem_list, list_num),
                        "HixlClient AddRemoteMem failed");
  }
  HIXL_LOGI("ProcessRemoteMem end");
  return SUCCESS;
}

void HixlClient::WaitBatchCsSyncInflightDrain() {
  while (batch_cs_sync_inflight_.load(std::memory_order_acquire) != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

Status HixlClient::FinalizeUnregisterAllMemHandles() {
  Status ret = SUCCESS;
  std::lock_guard<std::mutex> lock(client_handler_->GetMemHandleMutex());
  for (const auto &pair : client_handler_->GetMemHandles()) {
    ret = UnregisterMemToCsClient(pair.first, pair.second);
  }
  client_handler_->GetMemHandles().clear();
  return ret;
}

Status HixlClient::FinalizeDestroyAllCsClients() {
  Status ret = SUCCESS;
  std::lock_guard<std::mutex> lock(client_handler_->GetHandleMutex());
  for (const auto &pair : client_handler_->GetHandles()) {
    auto handle = pair.second;
    if (handle != nullptr) {
      auto status = HixlCSClientDestroy(handle);
      if (status != SUCCESS) {
        HIXL_LOGE(FAILED, "HixlClient Destroy Cs Client failed for type %s", CommTypeToString(pair.first));
        ret = status;
      }
    }
  }
  client_handler_->GetHandles().clear();
  return ret;
}

void HixlClient::FinalizeClearSharedResources() {
  if (client_handler_ != nullptr) {
    client_handler_->Clear();
  }
  {
    std::lock_guard<std::mutex> lock(complete_handles_mutex_);
    complete_handles_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_connected_ = false;
  }
}

Status HixlClient::Finalize() {
  HIXL_LOGI("HixlClient Finalize Start");
  Status ret = SUCCESS;
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (is_finalized_) {
      HIXL_LOGI("HixlClient Finalize skipped, already finalized");
      return SUCCESS;
    }
    finalize_pending_ = true;
  }
  WaitBatchCsSyncInflightDrain();
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_finalized_ = true;
  }
  ret = FinalizeUnregisterAllMemHandles();
  Status destroy_ret = FinalizeDestroyAllCsClients();
  if (destroy_ret != SUCCESS) {
    ret = destroy_ret;
  }
  FinalizeClearSharedResources();
  HIXL_LOGI("HixlClient Finalize End, status: %d", static_cast<int32_t>(ret));
  return ret;
}

Status HixlClient::UnregisterMemToCsClient(CommType type, const std::vector<MemHandle> &mem_handles) {
  std::lock_guard<std::mutex> lock(client_handler_->GetHandleMutex());
  auto handle_it = client_handler_->GetHandles().find(type);
  if (handle_it == client_handler_->GetHandles().end()) {
    HIXL_LOGE(FAILED, "No cs client handle found for type %s, skip mem unregistration", CommTypeToString(type));
    return FAILED;
  }
  auto handle = handle_it->second;
  if (handle == nullptr) {
    HIXL_LOGE(FAILED, "Client handle is nullptr for type %s, skip mem unregistration", CommTypeToString(type));
    return FAILED;
  }
  Status ret = SUCCESS;
  for (auto &mem_handle : mem_handles) {
    if (mem_handle != nullptr) {
      auto status = HixlCSClientUnregMem(handle_it->second, mem_handle);
      if (status != SUCCESS) {
        HIXL_LOGE(status, "HixlClient UnregMem failed for type %s", CommTypeToString(type));
        ret = status;
      }
    }
  }
  return ret;
}

Status HixlClient::BatchTransfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                 std::vector<TransferCompleteInfo> &complete_handle_list) {
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!is_connected_) {
      HIXL_LOGE(NOT_CONNECTED, "HixlClient is not connected");
      return NOT_CONNECTED;
    }
  }
  std::map<CommType, std::vector<TransferOpDesc>> op_descs_table;
  HIXL_CHK_STATUS_RET(client_handler_->ClassifyTransfers(op_descs, op_descs_table),
                      "HixlClient failed to classify transfer op_descs");

  std::lock_guard<std::mutex> lock(client_handler_->GetHandleMutex());
  for (const auto &type_with_op_descs : op_descs_table) {
    auto type = type_with_op_descs.first;
    const auto &op_descs_vec = type_with_op_descs.second;
    HIXL_LOGI("HixlClient BatchTransfer start, type:%s, op_descs size:%zu", CommTypeToString(type),
              op_descs_vec.size());
    HixlClientHandle handle = nullptr;
    auto it = client_handler_->GetHandles().find(type);
    if (it == client_handler_->GetHandles().end()) {
      HIXL_LOGE(FAILED, "HixlClient not found client handle for type:%s", CommTypeToString(type));
      return FAILED;
    } else {
      handle = it->second;
    }
    uint32_t list_num = op_descs_vec.size();
    std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
    for (size_t i = 0; i < list_num; i++) {
      hixl_descs[i].remote_buf = reinterpret_cast<void *>(op_descs_vec[i].remote_addr);
      hixl_descs[i].local_buf = reinterpret_cast<void *>(op_descs_vec[i].local_addr);
      hixl_descs[i].len = op_descs_vec[i].len;
    }
    CompleteHandle complete_handle = nullptr;
    if (operation == WRITE) {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchPutAsync(handle, list_num, hixl_descs.data(), &complete_handle),
                          "HixlClient BatchPutAsync failed, client_handle: %p", handle);
    } else {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchGetAsync(handle, list_num, hixl_descs.data(), &complete_handle),
                          "HixlClient BatchGetAsync failed, client_handle: %p", handle);
    }
    TransferCompleteInfo complete_info{type, complete_handle};
    complete_handle_list.push_back(complete_info);
  }
  return SUCCESS;
}

Status HixlClient::BatchTransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                     const std::chrono::steady_clock::time_point &sync_start, uint32_t timeout_ms) {
  const auto func_start = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!is_connected_) {
      HIXL_LOGE(NOT_CONNECTED, "HixlClient is not connected");
      return NOT_CONNECTED;
    }
  }
  const auto check_end = std::chrono::steady_clock::now();
  const auto check_us = std::chrono::duration_cast<std::chrono::microseconds>(check_end - func_start).count();

  std::map<CommType, std::vector<TransferOpDesc>> op_descs_table;
  HIXL_CHK_STATUS_RET(client_handler_->ClassifyTransfers(op_descs, op_descs_table),
                      "HixlClient failed to classify transfer op_descs");
  const auto classify_end = std::chrono::steady_clock::now();
  const auto classify_us = std::chrono::duration_cast<std::chrono::microseconds>(classify_end - check_end).count();

  for (const auto &type_with_op_descs : op_descs_table) {
    uint32_t remaining_timeout_ms = 0;
    HIXL_CHK_STATUS_RET(ComputeBatchSyncRemainingMs(sync_start, timeout_ms, remaining_timeout_ms),
                        "HixlClient BatchTransferSync exceeded timeout budget");
    const auto type = type_with_op_descs.first;
    const auto &op_descs_vec = type_with_op_descs.second;
    HixlClientHandle handle = nullptr;
    {
      std::lock_guard<std::mutex> ch_lock(client_handler_->GetHandleMutex());
      auto it = client_handler_->GetHandles().find(type);
      if (it == client_handler_->GetHandles().end()) {
        HIXL_LOGE(FAILED, "HixlClient not found client handle for type:%s", CommTypeToString(type));
        return FAILED;
      }
      handle = it->second;
    }
    HIXL_CHK_STATUS_RET(BatchTransferSyncInvoke(handle, type, op_descs_vec, operation, remaining_timeout_ms),
                        "HixlClient BatchTransferSync failed for client_handle: %p", handle);
  }
  const auto func_end = std::chrono::steady_clock::now();
  const auto transfer_us = std::chrono::duration_cast<std::chrono::microseconds>(func_end - classify_end).count();
  const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(func_end - func_start).count();
  HIXL_LOGE(SUCCESS, "[HixlClient] BatchTransferSync timing(us): total=%ld check=%ld classify=%ld transfer=%ld op_descs=%zu op=%s",
            total_us, check_us, classify_us, transfer_us, op_descs.size(), (operation == WRITE) ? "WRITE" : "READ");
  return SUCCESS;
}

Status HixlClient::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient TransferAsync failed, op_descs is empty");
    return PARAM_INVALID;
  }
  std::vector<TransferCompleteInfo> complete_handle_list;
  HIXL_CHK_STATUS_RET(BatchTransfer(op_descs, operation, complete_handle_list), "HixlClient TransferAsync failed");
  req = complete_handle_list[0].complete_handle;
  std::lock_guard<std::mutex> lock(complete_handles_mutex_);
  complete_handles_[req] = complete_handle_list;
  return SUCCESS;
}

Status HixlClient::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(complete_handles_mutex_);
  if (complete_handles_.empty()) {
    HIXL_LOGE(FAILED, "HixlClient GetTransferStatus failed, no transfer tasks in progress");
    status = TransferStatus::FAILED;
    return FAILED;
  }
  auto it = complete_handles_.find(req);
  if (it == complete_handles_.end()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient GetTransferStatus failed, invalid req");
    status = TransferStatus::FAILED;
    return PARAM_INVALID;
  }
  std::vector<TransferCompleteInfo> complete_handle_list = it->second;

  bool all_complete = true;
  for (const auto &type_with_complete_handle : complete_handle_list) {
    auto type = type_with_complete_handle.type;
    auto complete_handle = type_with_complete_handle.complete_handle;
    HixlCompleteStatus query_status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    Status ret = SUCCESS;
    {
      std::lock_guard<std::mutex> client_lock(client_handler_->GetHandleMutex());
      auto res = HixlCSClientQueryCompleteStatus(client_handler_->GetHandles()[type], complete_handle, &query_status);
      ret = static_cast<Status>(res);
    }
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "HixlClient QueryCompleteStatus failed");
      status = TransferStatus::FAILED;
      complete_handles_.erase(req);
      return ret;
    }
    if (query_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      continue;
    } else if (query_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING) {
      all_complete = false;
    }
  }
  if (all_complete) {
    HIXL_LOGI("BatchTransfer is completed");
    status = TransferStatus::COMPLETED;
    complete_handles_.erase(req);
    return SUCCESS;
  } else {
    HIXL_LOGI("BatchTransfer is running");
    status = TransferStatus::WAITING;
    return SUCCESS;
  }
}

Status HixlClient::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                uint32_t timeout_ms) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient TransferSync failed, op_descs is empty");
    return PARAM_INVALID;
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (is_finalized_ || finalize_pending_) {
      HIXL_LOGE(FAILED, "HixlClient TransferSync rejected, client is finalizing or finalized");
      return FAILED;
    }
    batch_cs_sync_inflight_.fetch_add(1, std::memory_order_acq_rel);
  }
  HIXL_DISMISSABLE_GUARD(sync_inflight_guard,
                         ([this]() { batch_cs_sync_inflight_.fetch_sub(1, std::memory_order_acq_rel); }));
  const auto start = std::chrono::steady_clock::now();
  HIXL_CHK_STATUS_RET(BatchTransferSync(op_descs, operation, start, timeout_ms), "HixlClient TransferSync failed");
  return SUCCESS;
}

}  // namespace hixl
