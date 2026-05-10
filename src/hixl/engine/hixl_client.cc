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
#include <cstring>
#include <thread>
#include "securec.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"
#include "engine/client_handler_factory.h"
#include "engine/endpoint_generator.h"

namespace hixl {
namespace {

constexpr uint64_t kMaxRecvRespBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);
constexpr uint32_t kCtrlMsgPluginTimeoutMs = 10000U;

}  // namespace

// ===== Initialize =====
Status HixlClient::Initialize(const std::vector<EndpointConfig> &local_endpoint_list) {
  if (local_endpoint_list.empty()) {
    HIXL_LOGE(PARAM_INVALID, "local_endpoint_list is empty");
    return PARAM_INVALID;
  }
  std::vector<EndpointConfig> remote_endpoint_list;
  CtrlMsgPlugin::Initialize();
  {
    int32_t socket = -1;
    HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, socket, kCtrlMsgPluginTimeoutMs));
    ScopeGuard sg([&]() { if (socket != -1) close(socket); });
    HIXL_CHK_STATUS_RET(SendEndpointInfoReq(socket, CtrlMsgType::kGetEndpointInfoReq));
    HIXL_CHK_STATUS_RET(RecvEndpointInfoResp(socket, remote_endpoint_list));
  }
  if (remote_endpoint_list.empty()) {
    HIXL_LOGE(FAILED, "received empty remote_endpoint_list");
    return FAILED;
  }
  HandlerCreateArgs args{server_ip_, server_port_, rdma_tc_, rdma_sl_,
                          local_endpoint_list, remote_endpoint_list};
  client_handler_ = ClientHandlerFactory::Create(args);
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "ClientHandlerFactory failed");
    return FAILED;
  }
  return SUCCESS;
}

Status HixlClient::SendEndpointInfoReq(int32_t fd, CtrlMsgType msg_type) const {
  CtrlMsgHeader hdr{kMagicNumber, static_cast<uint64_t>(sizeof(CtrlMsgType))};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &hdr, sizeof(hdr)));
  return CtrlMsgPlugin::Send(fd, &msg_type, sizeof(msg_type));
}

Status HixlClient::RecvEndpointInfoResp(int32_t fd, std::vector<EndpointConfig> &remote) const {
  CtrlMsgHeader hdr{};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &hdr, sizeof(hdr), kCtrlMsgPluginTimeoutMs));
  HIXL_CHK_BOOL_RET_STATUS(hdr.magic == kMagicNumber, PARAM_INVALID, "bad magic: 0x%X", hdr.magic);
  HIXL_CHK_BOOL_RET_STATUS(hdr.body_size > sizeof(CtrlMsgType) && hdr.body_size <= kMaxRecvRespBodySize,
                           PARAM_INVALID, "bad body_size: %" PRIu64, hdr.body_size);

  std::vector<uint8_t> body(hdr.body_size);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, body.data(), static_cast<uint32_t>(hdr.body_size), kCtrlMsgPluginTimeoutMs));

  CtrlMsgType msg_type{};
  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), body.data(), sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s failed, rc=%d", rc);
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kGetEndpointInfoResp, PARAM_INVALID,
                           "unexpected msg_type: %d", static_cast<int>(msg_type));

  std::string json(reinterpret_cast<const char *>(body.data() + sizeof(msg_type)),
                   static_cast<size_t>(hdr.body_size - sizeof(CtrlMsgType)));
  return EndpointGenerator::DeserializeEndpointConfigList(json, remote);
}

// ===== SetLocalMemInfo =====
Status HixlClient::SetLocalMemInfo(const std::vector<MemInfo> &mem_info_list) {
  for (const auto &mi : mem_info_list) {
    HIXL_CHK_STATUS_RET(client_handler_->RegisterMem(mi));
  }
  return SUCCESS;
}

// ===== Connect =====
Status HixlClient::Connect(uint32_t timeout_ms) {
  HIXL_CHK_STATUS_RET(client_handler_->Connect(timeout_ms));
  std::lock_guard<std::mutex> lock(status_mutex_);
  is_connected_ = true;
  return SUCCESS;
}

// ===== TransferSync =====
Status HixlClient::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                uint32_t timeout_ms) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "op_descs is empty");
    return PARAM_INVALID;
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!is_connected_) return NOT_CONNECTED;
    if (is_finalized_ || finalize_pending_) {
      HIXL_LOGE(FAILED, "client is finalizing or finalized");
      return FAILED;
    }
    batch_cs_sync_inflight_.fetch_add(1, std::memory_order_acq_rel);
  }
  HIXL_DISMISSABLE_GUARD(sync_inflight_guard,
                         ([this]() { batch_cs_sync_inflight_.fetch_sub(1, std::memory_order_acq_rel); }));
  return client_handler_->TransferSync(op_descs, operation, timeout_ms);
}

// ===== TransferAsync =====
Status HixlClient::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                  TransferReq &req) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "op_descs is empty");
    return PARAM_INVALID;
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!is_connected_) return NOT_CONNECTED;
  }
  std::vector<TransferCompleteInfo> complete_handle_list;
  HIXL_CHK_STATUS_RET(client_handler_->Transfer(op_descs, operation, complete_handle_list));
  req = complete_handle_list[0].complete_handle;
  std::lock_guard<std::mutex> lock(complete_handles_mutex_);
  complete_handles_[req] = std::move(complete_handle_list);
  return SUCCESS;
}

// ===== GetTransferStatus =====
Status HixlClient::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  std::lock_guard<std::mutex> lock(complete_handles_mutex_);
  auto it = complete_handles_.find(req);
  if (it == complete_handles_.end()) {
    HIXL_LOGE(PARAM_INVALID, "invalid req");
    status = TransferStatus::FAILED;
    return PARAM_INVALID;
  }
  bool all_complete = true;
  for (const auto &info : it->second) {
    HixlCompleteStatus cs = HIXL_COMPLETE_STATUS_WAITING;
    Status ret = client_handler_->QueryStatus(info.type, info.complete_handle, cs);
    if (ret != SUCCESS) {
      status = TransferStatus::FAILED;
      complete_handles_.erase(req);
      return ret;
    }
    if (cs == HIXL_COMPLETE_STATUS_WAITING) all_complete = false;
  }
  status = all_complete ? TransferStatus::COMPLETED : TransferStatus::WAITING;
  if (all_complete) complete_handles_.erase(req);
  return SUCCESS;
}

// ===== Finalize =====
void HixlClient::WaitBatchCsSyncInflightDrain() {
  while (batch_cs_sync_inflight_.load(std::memory_order_acquire) != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

Status HixlClient::Finalize() {
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (is_finalized_) return SUCCESS;
    finalize_pending_ = true;
  }
  WaitBatchCsSyncInflightDrain();
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_finalized_ = true;
  }
  Status ret = (client_handler_ != nullptr) ? client_handler_->Finalize() : SUCCESS;
  {
    std::lock_guard<std::mutex> lock(complete_handles_mutex_);
    complete_handles_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_connected_ = false;
  }
  return ret;
}

}  // namespace hixl
