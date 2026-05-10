/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/direct_client_handler.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {

DirectClientHandler::DirectClientHandler(std::map<CommType, HixlClientHandle> handles)
    : handles_(std::move(handles)) {}

HixlClientHandle DirectClientHandler::SingleHandle() const { return handles_.begin()->second; }
CommType DirectClientHandler::SingleType() const { return handles_.begin()->first; }

Status DirectClientHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<Status>(HixlCSClientConnect(SingleHandle(), timeout_ms));
}

Status DirectClientHandler::FetchRemoteMem(uint32_t timeout_ms) {
  (void)timeout_ms;
  return SUCCESS;
}

Status DirectClientHandler::RegisterMem(const MemInfo &mem_info) {
  CommMem hccl_mem{};
  hccl_mem.type = (mem_info.type == MemType::MEM_DEVICE) ? COMM_MEM_TYPE_DEVICE : COMM_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem_info.mem.addr);
  hccl_mem.size = mem_info.mem.len;

  std::lock_guard<std::mutex> lock(mutex_);
  MemHandle mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(HixlCSClientRegMem(SingleHandle(), nullptr, &hccl_mem, &mem_handle),
                      "DirectClientHandler register memory failed, addr: 0x%lx", mem_info.mem.addr);
  mem_handles_[SingleType()].push_back(mem_handle);
  return SUCCESS;
}

Status DirectClientHandler::Transfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                      std::vector<TransferCompleteInfo> &complete_handle_list) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto type = SingleType();
  auto handle = SingleHandle();
  uint32_t list_num = static_cast<uint32_t>(op_descs.size());
  std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
  for (size_t i = 0; i < list_num; i++) {
    hixl_descs[i].remote_buf = reinterpret_cast<void *>(op_descs[i].remote_addr);
    hixl_descs[i].local_buf = reinterpret_cast<void *>(op_descs[i].local_addr);
    hixl_descs[i].len = op_descs[i].len;
  }
  CompleteHandle complete_handle = nullptr;
  if (operation == WRITE) {
    HIXL_CHK_STATUS_RET(HixlCSClientBatchPutAsync(handle, list_num, hixl_descs.data(), &complete_handle));
  } else {
    HIXL_CHK_STATUS_RET(HixlCSClientBatchGetAsync(handle, list_num, hixl_descs.data(), &complete_handle));
  }
  complete_handle_list.push_back({type, complete_handle});
  return SUCCESS;
}

Status DirectClientHandler::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                          uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto handle = SingleHandle();
  uint32_t list_num = static_cast<uint32_t>(op_descs.size());
  std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
  for (size_t i = 0; i < list_num; i++) {
    hixl_descs[i].remote_buf = reinterpret_cast<void *>(op_descs[i].remote_addr);
    hixl_descs[i].local_buf = reinterpret_cast<void *>(op_descs[i].local_addr);
    hixl_descs[i].len = op_descs[i].len;
  }
  if (operation == WRITE) {
    return static_cast<Status>(HixlCSClientBatchPutSync(handle, list_num, hixl_descs.data(), timeout_ms));
  }
  return static_cast<Status>(HixlCSClientBatchGetSync(handle, list_num, hixl_descs.data(), timeout_ms));
}

Status DirectClientHandler::QueryStatus(CommType type, CompleteHandle handle, HixlCompleteStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(type);
  if (it == handles_.end()) return FAILED;
  return static_cast<Status>(HixlCSClientQueryCompleteStatus(it->second, handle, &status));
}

Status DirectClientHandler::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &[type, mem_list] : mem_handles_) {
    auto it = handles_.find(type);
    if (it == handles_.end()) continue;
    for (auto &mh : mem_list) {
      if (mh != nullptr) HixlCSClientUnregMem(it->second, mh);
    }
  }
  mem_handles_.clear();
  for (auto &pair : handles_) {
    if (pair.second != nullptr) HixlCSClientDestroy(pair.second);
  }
  handles_.clear();
  return SUCCESS;
}

}  // namespace hixl
