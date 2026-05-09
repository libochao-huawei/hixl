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

Status DirectClientHandler::ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                                                  std::map<CommType, std::vector<TransferOpDesc>> &table) {
  if (handles_.empty()) {
    HIXL_LOGE(FAILED, "DirectClientHandler has no handle");
    return FAILED;
  }
  table[handles_.begin()->first] = op_descs;
  return SUCCESS;
}

Status DirectClientHandler::ProcessLocalMem(const MemInfo &mem_info, const std::string &server_ip,
                                                uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  (void)server_ip;
  (void)server_port;
  (void)rdma_tc;
  (void)rdma_sl;
  CommMem hccl_mem{};
  hccl_mem.type = (mem_info.type == MemType::MEM_DEVICE) ? COMM_MEM_TYPE_DEVICE : COMM_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem_info.mem.addr);
  hccl_mem.size = mem_info.mem.len;

  std::lock_guard<std::mutex> lock(handle_mutex_);
  if (handles_.empty()) {
    HIXL_LOGE(FAILED, "DirectClientHandler has no handle for mem registration");
    return FAILED;
  }
  auto handle = handles_.begin()->second;
  MemHandle mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(HixlCSClientRegMem(handle, nullptr, &hccl_mem, &mem_handle),
                      "DirectClientHandler register memory failed, handle: %p, addr: 0x%lx, size: %lu",
                      handle, mem_info.mem.addr, mem_info.mem.len);

  std::lock_guard<std::mutex> mem_lock(mem_handle_mutex_);
  mem_handles_[handles_.begin()->first].push_back(mem_handle);
  return SUCCESS;
}

Status DirectClientHandler::AddRemoteMem(CommMem *remote_mem_list, uint32_t list_num) {
  (void)remote_mem_list;
  (void)list_num;
  return SUCCESS;
}

void DirectClientHandler::Clear() {}

}  // namespace hixl
