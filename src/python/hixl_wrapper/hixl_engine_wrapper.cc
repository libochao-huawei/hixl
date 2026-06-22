/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_engine_wrapper.h"
#include "common/hixl_checker.h"

namespace hixl_wrapper {

std::unique_ptr<hixl::Hixl> HixlEngineWrapper::hixl_engine_;
std::shared_mutex HixlEngineWrapper::mutex_;

hixl::Status HixlEngineWrapper::Initialize(
    const std::string &local_engine,
    const std::map<std::string, std::string> &options) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHK_BOOL_RET_STATUS(hixl_engine_ == nullptr, hixl::FAILED, "Repeat Init");
  auto instance = std::make_unique<hixl::Hixl>();
  HIXL_CHECK_NOTNULL(instance);
  hixl::AscendString ascend_local_engine(local_engine.c_str());
  std::map<hixl::AscendString, hixl::AscendString> ascend_options;
  for (const auto &opt : options) {
    (void)ascend_options.emplace(opt.first.c_str(),opt.second.c_str());
  }
  HIXL_CHK_STATUS_RET(instance->Initialize(ascend_local_engine, ascend_options));
  hixl_engine_ = std::move(instance);
  return hixl::SUCCESS;
}

void HixlEngineWrapper::Finalize() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (hixl_engine_ != nullptr) {
    hixl_engine_->Finalize();
    hixl_engine_.reset();
  }
}

std::pair<hixl::Status, hixl::MemType> HixlEngineWrapper::ParseMemType(const std::string &mem_type_str) {
  if (mem_type_str == "npu") return {hixl::SUCCESS, hixl::MEM_DEVICE};
  if (mem_type_str == "cpu") return {hixl::SUCCESS, hixl::MEM_HOST};
  HIXL_LOGW("ParseMemType: invalid mem_type '%s', expected 'npu' or 'cpu'",
            mem_type_str.c_str());
  return {hixl::PARAM_INVALID, hixl::MEM_DEVICE};
}

std::pair<hixl::Status, hixl::TransferOp> HixlEngineWrapper::ParseTransferOp(const std::string &op_str) {
  if (op_str == "READ")  return {hixl::SUCCESS, hixl::READ};
  if (op_str == "WRITE") return {hixl::SUCCESS, hixl::WRITE};
  HIXL_LOGW("ParseTransferOp: invalid operation '%s', expected 'READ' or 'WRITE'",
            op_str.c_str());
  return {hixl::PARAM_INVALID, hixl::READ};
}

std::string HixlEngineWrapper::TransferStatusToStr(hixl::TransferStatus status) {
  switch (status) {
    case hixl::TransferStatus::WAITING:   return "WAITING";
    case hixl::TransferStatus::COMPLETED: return "COMPLETED";
    case hixl::TransferStatus::TIMEOUT:   return "TIMEOUT";
    case hixl::TransferStatus::FAILED:    return "FAILED";
    default:                              return "UNKNOWN";
  }
}

std::string HixlEngineWrapper::AsyncConnectStatusToStr(hixl::AsyncConnectStatus status) {
  switch (status) {
    case hixl::AsyncConnectStatus::NOT_CONNECT:        return "NOT_CONNECT";
    case hixl::AsyncConnectStatus::CONNECT_PENDING:    return "CONNECT_PENDING";
    case hixl::AsyncConnectStatus::CONNECTING:         return "CONNECTING";
    case hixl::AsyncConnectStatus::CONNECTED:          return "CONNECTED";
    case hixl::AsyncConnectStatus::CONNECT_FAILED:     return "CONNECT_FAILED";
    case hixl::AsyncConnectStatus::DISCONNECT_PENDING: return "DISCONNECT_PENDING";
    case hixl::AsyncConnectStatus::DISCONNECTING:      return "DISCONNECTING";
    default:                                           return "UNKNOWN";
  }
}

std::pair<hixl::Status, hixl::FeatureType> HixlEngineWrapper::ParseFeatureType(
    const std::string &feature_type_str) {
  if (feature_type_str == "auto_connect")       return {hixl::SUCCESS, hixl::AUTO_CONNECT};
  if (feature_type_str == "client_server_comm") return {hixl::SUCCESS, hixl::CLIENT_SERVER_COMM};
  HIXL_LOGW("ParseFeatureType: invalid feature_type '%s', "
            "expected 'auto_connect' or 'client_server_comm'", feature_type_str.c_str());
  return {hixl::PARAM_INVALID, hixl::AUTO_CONNECT};
}

hixl::MemDesc HixlEngineWrapper::UnpackMemDesc(const MemDescTuple &t) {
  hixl::MemDesc mem_desc{};
  mem_desc.addr = std::get<0>(t);
  mem_desc.len  = std::get<1>(t);
  return mem_desc;
}

std::vector<hixl::TransferOpDesc> HixlEngineWrapper::UnpackTransferOpDescs(
    const std::vector<TransferOpDescTuple> &op_desc_tuples) {
  std::vector<hixl::TransferOpDesc> op_descs;
  op_descs.reserve(op_desc_tuples.size());
  for (const auto &t : op_desc_tuples) {
    hixl::TransferOpDesc desc{};
    desc.local_addr  = std::get<0>(t);
    desc.remote_addr = std::get<1>(t);
    desc.len         = std::get<2>(t);
    (void)op_descs.emplace_back(desc);
  }
  return op_descs;
}

hixl::NotifyDesc HixlEngineWrapper::UnpackNotifyDesc(const NotifyDescTuple &t) {
  hixl::NotifyDesc notify{};
  notify.name       = hixl::AscendString(std::get<0>(t).c_str());
  notify.notify_msg = hixl::AscendString(std::get<1>(t).c_str());
  return notify;
}

std::pair<hixl::Status, uintptr_t> HixlEngineWrapper::RegisterMem(
    const MemDescTuple &mem_desc_tuple, const std::string &mem_type_str) {
  hixl::MemHandle handle = nullptr;
  auto [parse_status, mem_type] = ParseMemType(mem_type_str);
  if (parse_status != hixl::SUCCESS) {
    return {parse_status, reinterpret_cast<uintptr_t>(handle)};
  }
  auto mem_desc = UnpackMemDesc(mem_desc_tuple);
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    HIXL_CHECK_NOTNULL(hixl_engine_);
    auto ret = hixl_engine_->RegisterMem(mem_desc, mem_type, handle);
  }
  return {ret, reinterpret_cast<uintptr_t>(handle)};
}

hixl::Status HixlEngineWrapper::DeregisterMem(uintptr_t mem_handle) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(hixl_engine_);
  hixl::MemHandle handle = reinterpret_cast<hixl::MemHandle>(mem_handle);
  return hixl_engine_->DeregisterMem(handle);
}

hixl::Status HixlEngineWrapper::Connect(const std::string &remote_engine, int32_t timeout_ms) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(hixl_engine_);
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->Connect(ascend_remote, timeout_ms);
}

hixl::Status HixlEngineWrapper::Disconnect(const std::string &remote_engine, int32_t timeout_ms) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(hixl_engine_);
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->Disconnect(ascend_remote, timeout_ms);
}

hixl::Status HixlEngineWrapper::ConnectAsync(const std::string &remote_engine, int32_t timeout_ms) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(hixl_engine_);
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->ConnectAsync(ascend_remote, timeout_ms);
}

hixl::Status HixlEngineWrapper::DisconnectAsync(const std::string &remote_engine, int32_t timeout_ms) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(hixl_engine_);
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->DisconnectAsync(ascend_remote, timeout_ms);
}

std::pair<hixl::Status, std::string> HixlEngineWrapper::GetAsyncConnectStatus(
    const std::string &remote_engine) {
  hixl::AsyncConnectStatus status = hixl::AsyncConnectStatus::NOT_CONNECT;
  hixl::Status ret = hixl::FAILED;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    HIXL_CHECK_NOTNULL(hixl_engine_);
    hixl::AscendString ascend_remote(remote_engine.c_str());
    ret = hixl_engine_->GetAsyncConnectStatus(ascend_remote, status);
  }
  return {ret, AsyncConnectStatusToStr(status)};
}

std::pair<hixl::Status, std::vector<std::pair<std::string, std::string>>>
HixlEngineWrapper::GetAllAsyncConnectStatus() {
  hixl::Status ret = hixl::FAILED;
  std::map<hixl::AscendString, hixl::AsyncConnectStatus> statuses;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    HIXL_CHECK_NOTNULL(hixl_engine_);
    ret = hixl_engine_->GetAsyncConnectStatus(statuses);
  }
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto &s : statuses) {
    (void)result.emplace_back(std::make_pair(std::string(s.first.GetString()),
                                       AsyncConnectStatusToStr(s.second)));
  }
  return {ret, result};
}

hixl::Status HixlEngineWrapper::TransferSync(
    const std::string &remote_engine,
    const std::string &operation,
    const std::vector<TransferOpDescTuple> &op_desc_tuples,
    int32_t timeout_ms) {
  auto [parse_status, op] = ParseTransferOp(operation);
  if (parse_status != hixl::SUCCESS) {
    return parse_status;
  }
  auto op_descs = UnpackTransferOpDescs(op_desc_tuples);
  std::shared_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(hixl_engine_);
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->TransferSync(ascend_remote, op, op_descs, timeout_ms);
}

std::pair<hixl::Status, uintptr_t> HixlEngineWrapper::TransferAsync(
    const std::string &remote_engine,
    const std::string &operation,
    const std::vector<TransferOpDescTuple> &op_desc_tuples) {
  hixl::TransferReq req = nullptr;
  auto [parse_status, op] = ParseTransferOp(operation);
  if (parse_status != hixl::SUCCESS) {
    return {parse_status, reinterpret_cast<uintptr_t>(req)};
  }
  auto op_descs = UnpackTransferOpDescs(op_desc_tuples);
  hixl::TransferArgs args{};  // reserved[120] 默认为 0
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    HIXL_CHECK_NOTNULL(hixl_engine_);
    hixl::AscendString ascend_remote(remote_engine.c_str());
    auto ret = hixl_engine_->TransferAsync(ascend_remote, op, op_descs, args, req);
  }
  return {ret, reinterpret_cast<uintptr_t>(req)};
}

std::pair<hixl::Status, std::string> HixlEngineWrapper::GetTransferStatus(uintptr_t req_id) {
  hixl::TransferStatus status = hixl::TransferStatus::FAILED;
  hixl::Status ret = hixl::FAILED;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    HIXL_CHECK_NOTNULL(hixl_engine_);
    hixl::TransferReq req = reinterpret_cast<hixl::TransferReq>(req_id);
    ret = hixl_engine_->GetTransferStatus(req, status);
  }
  return {ret, TransferStatusToStr(status)};
}

std::pair<hixl::Status, std::vector<std::tuple<uintptr_t, std::string>>>
HixlEngineWrapper::GetTransferStatusBatch(uint32_t max_query_count, bool skip_waiting) {
  hixl::Status ret = hixl::FAILED;
  std::vector<hixl::TransferResult> results;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    HIXL_CHECK_NOTNULL(hixl_engine_);
    hixl::GetTransferStatusArgs args{};
    args.max_query_count = max_query_count;
    args.skip_waiting = skip_waiting;
    ret = hixl_engine_->GetTransferStatus(args, results);
  }
  std::vector<std::tuple<uintptr_t, std::string>> result_tuples;
  for (const auto &r : results) {
    (void)result_tuples.emplace_back(std::make_tuple(
        reinterpret_cast<uintptr_t>(r.req),
        TransferStatusToStr(r.status)));
  }
  return {ret, result_tuples};
}

hixl::Status HixlEngineWrapper::SendNotify(
    const std::string &remote_engine,
    const NotifyDescTuple &notify_tuple,
    int32_t timeout_ms) {
  auto notify = UnpackNotifyDesc(notify_tuple);
  std::shared_lock<std::shared_mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(hixl_engine_);
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->SendNotify(ascend_remote, notify, timeout_ms);
}

std::pair<hixl::Status, std::vector<NotifyDescTuple>> HixlEngineWrapper::GetNotifies() {
  hixl::Status ret = hixl::FAILED;
  std::vector<hixl::NotifyDesc> notifies;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    HIXL_CHECK_NOTNULL(hixl_engine_);
    ret = hixl_engine_->GetNotifies(notifies);
  }
  std::vector<NotifyDescTuple> notify_tuples;
  for (const auto &n : notifies) {
    std::string name(n.name.GetString());
    std::string msg(n.notify_msg.GetString());
    (void)notify_tuples.emplace_back(std::make_tuple(name, msg));
  }
  return {ret, notify_tuples};
}

std::pair<hixl::Status, int32_t> HixlEngineWrapper::GetCapability(const std::string &feature_type_str) {
  int32_t value = hixl::FEATURE_NOT_SUPPORTED;
  auto [parse_status, feature_type] = ParseFeatureType(feature_type_str);
  if (parse_status != hixl::SUCCESS) {
    return {parse_status, value};
  }
  auto ret = hixl::Hixl::GetCapability(feature_type, value);
  return {ret, value};
}

}  // namespace hixl_wrapper
