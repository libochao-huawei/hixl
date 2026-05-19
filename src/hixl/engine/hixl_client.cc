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
#include "engine/endpoint_matcher.h"

namespace hixl {
namespace {
constexpr uint64_t kMaxRecvRespBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);
constexpr uint32_t kCtrlMsgPluginTimeoutMs = 10000U;
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
  std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
  HandlerCreateArgs::HandlerType handler_type;
  HIXL_CHK_STATUS_RET(EndpointMatcher::MatchEndpoints(local_endpoint_list, remote_endpoint_list,
                                                      matched_pairs, handler_type),
                      "EndpointMatcher::MatchEndpoints failed");
  HandlerCreateArgs args{server_ip_, server_port_, rdma_tc_, rdma_sl_, handler_type, std::move(matched_pairs)};
  client_handler_ = ClientHandlerFactory::Create(args);
  HIXL_CHECK_NOTNULL(client_handler_, "ClientHandlerFactory create handler failed");
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
  for (const auto &mi : mem_info_list) {
    HIXL_CHK_STATUS_RET(client_handler_->RegisterMem(mi));
  }
  return SUCCESS;
}

Status HixlClient::Connect(uint32_t timeout_ms) {
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    return FAILED;
  }
  HIXL_CHK_STATUS_RET(client_handler_->Connect(timeout_ms));
  std::lock_guard<std::mutex> lock(status_mutex_);
  is_connected_ = true;
  return SUCCESS;
}

Status HixlClient::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                uint32_t timeout_ms) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient TransferSync failed, op_descs is empty");
    return PARAM_INVALID;
  }
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    return FAILED;
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!is_connected_) {
      HIXL_LOGE(NOT_CONNECTED, "HixlClient is not connected");
      return NOT_CONNECTED;
    }
    if (is_finalized_ || finalize_pending_) {
      HIXL_LOGE(FAILED, "HixlClient TransferSync rejected, client is finalizing or finalized");
      return FAILED;
    }
    batch_cs_sync_inflight_.fetch_add(1, std::memory_order_acq_rel);
  }
  HIXL_DISMISSABLE_GUARD(sync_inflight_guard,
                         ([this]() { batch_cs_sync_inflight_.fetch_sub(1, std::memory_order_acq_rel); }));
  return client_handler_->TransferSync(op_descs, operation, timeout_ms);
}

Status HixlClient::TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req) {
  if (op_descs.empty()) {
    HIXL_LOGE(PARAM_INVALID, "HixlClient TransferAsync failed, op_descs is empty");
    return PARAM_INVALID;
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!is_connected_) {
      HIXL_LOGE(NOT_CONNECTED, "HixlClient is not connected");
      return NOT_CONNECTED;
    }
  }
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    return FAILED;
  }
  return client_handler_->TransferAsync(op_descs, operation, req);
}

Status HixlClient::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  if (client_handler_ == nullptr) {
    HIXL_LOGE(FAILED, "HixlClient is not initialized");
    status = TransferStatus::FAILED;
    return FAILED;
  }
  return client_handler_->GetTransferStatus(req, status);
}

void HixlClient::WaitBatchCsSyncInflightDrain() {
  while (batch_cs_sync_inflight_.load(std::memory_order_acquire) != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

Status HixlClient::Finalize() {
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (is_finalized_) {
      return SUCCESS;
    }
    finalize_pending_ = true;
  }
  WaitBatchCsSyncInflightDrain();
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_finalized_ = true;
  }
  Status ret = (client_handler_ != nullptr) ? client_handler_->Finalize() : SUCCESS;
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    is_connected_ = false;
  }
  return ret;
}

Status HixlClient::RecvNotifyAck(int32_t fd, int32_t timeout_ms) {
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, &header, static_cast<uint32_t>(sizeof(header)), timeout_ms),
                      "HixlClient receive NotifyAck header failed, fd:%d", fd);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID,
                           "Invalid magic for NotifyAck, expect:0x%X, actual:0x%X", kMagicNumber, header.magic);
  HIXL_CHK_BOOL_RET_STATUS(header.body_size > sizeof(CtrlMsgType) && header.body_size <= kMaxRecvRespBodySize,
                           PARAM_INVALID, "Invalid body_size for NotifyAck, body_size:%lu", header.body_size);

  const uint64_t body_size = header.body_size;
  std::vector<uint8_t> body(body_size);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(fd, body.data(), static_cast<uint32_t>(body_size), timeout_ms),
                      "HixlClient receive NotifyAck body failed, fd:%d", fd);

  CtrlMsgType msg_type{};
  const void *src = static_cast<const void *>(body.data());
  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "memcpy_s msg_type failed, rc:%d", static_cast<int32_t>(rc));
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kNotifyAck, PARAM_INVALID,
                           "Unexpected msg_type=%d, expect kNotifyAck=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(CtrlMsgType::kNotifyAck));

  const size_t json_len = static_cast<size_t>(body_size - sizeof(CtrlMsgType));
  std::string json_str(reinterpret_cast<const char *>(body.data() + sizeof(msg_type)), json_len);
  NotifyAck ack;
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    ack = j.get<NotifyAck>();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse NotifyAck, exception:%s", e.what());
    return PARAM_INVALID;
  }
  if (ack.result != SUCCESS) {
    HIXL_LOGE(ack.result, "NotifyAck result failed, result:%u", ack.result);
    return ack.result;
  }
  HIXL_LOGI("HixlClient received NotifyAck success");
  return SUCCESS;
}

Status HixlClient::SendNotify(const NotifyDesc &notify, int32_t timeout_ms) {
  std::string name = notify.name.GetString();
  std::string msg = notify.notify_msg.GetString();

  if (name.empty() || name.size() > kMaxNotifyNameLen) {
    HIXL_LOGE(PARAM_INVALID, "Notify name length invalid, size:%zu, max:%zu", name.size(), kMaxNotifyNameLen);
    return PARAM_INVALID;
  }
  if (msg.size() > kMaxNotifyMsgLen) {
    HIXL_LOGE(PARAM_INVALID, "Notify message too long, size:%zu, max:%zu", msg.size(), kMaxNotifyMsgLen);
    return PARAM_INVALID;
  }

  NotifyMsg notify_msg;
  notify_msg.name = name;
  notify_msg.notify_msg = msg;

  nlohmann::json j = notify_msg;
  std::string msg_str = j.dump();

  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + msg_str.size());
  CtrlMsgType msg_type = CtrlMsgType::kNotify;

  int32_t tmp_socket = -1;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, tmp_socket, timeout_ms),
                      "HixlClient create temp socket failed for SendNotify");
  HIXL_DISMISSABLE_GUARD(close_socket_guard, ([tmp_socket]() {
    if (tmp_socket >= 0) {
      close(tmp_socket);
    }
  }));

  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(tmp_socket, &header, static_cast<uint64_t>(sizeof(header))),
                      "HixlClient send NotifyMsg header failed, socket:%d", tmp_socket);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(tmp_socket, &msg_type, static_cast<uint64_t>(sizeof(msg_type))),
                      "HixlClient send NotifyMsg msg_type failed, socket:%d", tmp_socket);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(tmp_socket, msg_str.c_str(), static_cast<uint64_t>(msg_str.size())),
                      "HixlClient send NotifyMsg body failed, socket:%d", tmp_socket);

  HIXL_LOGI("HixlClient sent NotifyMsg, name:%s, socket:%d", notify_msg.name.c_str(), tmp_socket);
  HIXL_CHK_STATUS_RET(RecvNotifyAck(tmp_socket, timeout_ms),
                      "HixlClient receive NotifyAck failed, timeout:%d ms, socket:%d", timeout_ms, tmp_socket);

  HIXL_DISMISS_GUARD(close_socket_guard);
  close(tmp_socket);
  HIXL_LOGI("HixlClient closed temp socket:%d after SendNotify", tmp_socket);
  return SUCCESS;
}
}  // namespace hixl
