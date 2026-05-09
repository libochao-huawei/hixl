/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/ub_client_handler.h"
#include <algorithm>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"

namespace hixl {
namespace {

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

}  // namespace

Status UbClientHandler::ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                                              std::map<CommType, std::vector<TransferOpDesc>> &op_descs_table) {
  for (const auto &op_desc : op_descs) {
    MemType local_mem_type;
    {
      std::lock_guard<std::mutex> lock(local_seg_mutex_);
      if (GetMemType(local_segments_, op_desc.local_addr, op_desc.len, local_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Local memory range does not register before connection: start:0x%lx, end:0x%lx",
                  op_desc.local_addr, op_desc.local_addr + op_desc.len);
        return PARAM_INVALID;
      }
    }
    MemType remote_mem_type;
    {
      std::lock_guard<std::mutex> lock(remote_seg_mutex_);
      if (GetMemType(remote_segments_, op_desc.remote_addr, op_desc.len, remote_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Remote memory range does not register before connection: start:0x%lx, end:0x%lx",
                  op_desc.remote_addr, op_desc.remote_addr + op_desc.len);
        return PARAM_INVALID;
      }
    }

    CommType cur_type;
    if (local_mem_type == MEM_DEVICE) {
      cur_type = (remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_D2D : CommType::COMM_TYPE_UB_D2H;
    } else {
      cur_type = (remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_H2D : CommType::COMM_TYPE_UB_H2H;
    }
    op_descs_table[cur_type].push_back(op_desc);
    HIXL_LOGI("Current communication type: %s, local memory type: %s, remote memory type: %s.",
              CommTypeToString(cur_type), (local_mem_type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost,
              (remote_mem_type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
  }
  return SUCCESS;
}

Status UbClientHandler::GetMemType(const std::vector<SegmentPtr> &segments, uintptr_t addr,
                                       size_t len, MemType &mem_type) const {
  for (const auto &segment : segments) {
    if (segment->Contains(addr, addr + len)) {
      mem_type = segment->GetMemType();
      return SUCCESS;
    }
  }
  return PARAM_INVALID;
}

Status UbClientHandler::ProcessLocalMem(const MemInfo &mem_info, const std::string &server_ip,
                                            uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  (void)server_ip;
  (void)server_port;
  (void)rdma_tc;
  (void)rdma_sl;

  const auto &mem = mem_info.mem;
  const auto type = mem_info.type;

  HIXL_LOGI("UbClientHandler Add range to local_segments_, addr: 0x%lx, size: %lu, type: %s", mem.addr, mem.len,
            (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);

  {
    std::lock_guard<std::mutex> lock(local_seg_mutex_);
    auto seg_it = std::find_if(local_segments_.begin(), local_segments_.end(),
                               [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
    if (seg_it != local_segments_.end()) {
      HIXL_CHK_STATUS_RET((*seg_it)->AddRange(mem.addr, mem.len),
                          "Failed to add range to local_segments_, addr: 0x%lx, size: %lu, type: %s", mem.addr, mem.len,
                          (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
    } else {
      auto new_segment = MakeShared<Segment>(type);
      HIXL_CHK_BOOL_RET_STATUS(new_segment != nullptr, FAILED, "Failed to create new segment for type:%s",
                               (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
      HIXL_CHK_STATUS_RET(new_segment->AddRange(mem.addr, mem.len),
                          "Failed to add range to local_segments_, addr: 0x%lx, size: %lu, type: %s", mem.addr, mem.len,
                          (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
      local_segments_.push_back(new_segment);
    }
  }

  CommMem hccl_mem{};
  hccl_mem.type = (type == MemType::MEM_DEVICE) ? COMM_MEM_TYPE_DEVICE : COMM_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
  hccl_mem.size = mem.len;

  std::vector<CommType> comm_types_to_register;
  if (type == MemType::MEM_DEVICE) {
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_D2H);
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_D2D);
  } else {
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_H2D);
    comm_types_to_register.push_back(CommType::COMM_TYPE_UB_H2H);
  }

  std::lock_guard<std::mutex> lock(handle_mutex_);
  for (const auto &comm_type : comm_types_to_register) {
    auto handle_it = handles_.find(comm_type);
    if (handle_it == handles_.end()) {
      continue;
    }
    MemHandle mem_handle = nullptr;
    HIXL_CHK_STATUS_RET(HixlCSClientRegMem(handle_it->second, nullptr, &hccl_mem, &mem_handle),
                        "UbClientHandler register memory failed, handle: %p, addr: 0x%lx, size: %lu, type: %s",
                        handle_it->second, mem.addr, mem.len,
                        (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
    HIXL_LOGI("UbClientHandler register memory success, handle: %p, mem_handle: %p, addr: 0x%lx, size: %lu, type: %s",
              handle_it->second, mem_handle, mem.addr, mem.len,
              (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
    {
      std::lock_guard<std::mutex> mem_lock(mem_handle_mutex_);
      mem_handles_[comm_type].push_back(mem_handle);
    }
  }
  return SUCCESS;
}

Status UbClientHandler::AddRemoteMem(CommMem *remote_mem_list, uint32_t list_num) {
  std::lock_guard<std::mutex> lock(remote_seg_mutex_);
  for (uint32_t i = 0; i < list_num; i++) {
    MemType type = (remote_mem_list[i].type == COMM_MEM_TYPE_DEVICE) ? MEM_DEVICE : MEM_HOST;
    auto it = std::find_if(remote_segments_.begin(), remote_segments_.end(),
                           [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
    if (it != remote_segments_.end()) {
      HIXL_CHK_STATUS_RET(
          (*it)->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size),
          "Failed to add range to remote_segments_, addr: 0x%lx, size: %lu, type: %s",
          reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size,
          (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
    } else {
      auto new_segment = MakeShared<Segment>(type);
      HIXL_CHK_BOOL_RET_STATUS(new_segment != nullptr, FAILED, "Failed to create new segment for type:%s",
                               (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
      HIXL_CHK_STATUS_RET(
          new_segment->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size),
          "Failed to add range to remote_segments_, addr: 0x%lx, size: %lu, type: %s",
          reinterpret_cast<uintptr_t>(remote_mem_list[i].addr), remote_mem_list[i].size,
          (type == MemType::MEM_DEVICE) ? kMemTypeDevice : kMemTypeHost);
      remote_segments_.push_back(new_segment);
    }
  }
  return SUCCESS;
}

void UbClientHandler::Clear() {
  {
    std::lock_guard<std::mutex> lock(local_seg_mutex_);
    local_segments_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(remote_seg_mutex_);
    remote_segments_.clear();
  }
}

}  // namespace hixl
