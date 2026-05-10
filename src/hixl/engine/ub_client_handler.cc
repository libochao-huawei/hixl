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
#include "cs/hixl_cs_client.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/thread_pool.h"

namespace hixl {
namespace {

constexpr const char *kMemTypeDevice = "DEVICE";
constexpr const char *kMemTypeHost = "HOST";

const char *CommTypeToString(CommType type) {
  switch (type) {
    case CommType::COMM_TYPE_UB_D2D: return "UB_D2D";
    case CommType::COMM_TYPE_UB_H2D: return "UB_H2D";
    case CommType::COMM_TYPE_UB_D2H: return "UB_D2H";
    case CommType::COMM_TYPE_UB_H2H: return "UB_H2H";
    case CommType::COMM_TYPE_ROCE:   return "ROCE";
    case CommType::COMM_TYPE_HCCS:   return "HCCS";
    case CommType::COMM_TYPE_UBOE:   return "UBOE";
    default:                         return "UNKNOWN";
  }
}

Status ComputeRemainingMs(const std::chrono::steady_clock::time_point &start, uint32_t timeout_ms,
                          uint32_t &remaining_ms) {
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start).count();
  if (elapsed >= static_cast<int64_t>(timeout_ms)) {
    return TIMEOUT;
  }
  remaining_ms = static_cast<uint32_t>(static_cast<int64_t>(timeout_ms) - elapsed);
  return SUCCESS;
}

}  // namespace

UbClientHandler::UbClientHandler(std::map<CommType, HixlClientHandle> handles) : handles_(std::move(handles)) {}

Status UbClientHandler::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(handle_mutex_);
  if (handles_.empty()) return FAILED;

  ThreadPool thread_pool("ub_connect", handles_.size());
  std::vector<std::future<Status>> futures;
  aclrtContext context = nullptr;
  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&context));

  for (const auto &[type, handle] : handles_) {
    futures.emplace_back(thread_pool.commit([handle, timeout_ms, type, context]() -> Status {
      HIXL_CHK_ACL_RET(aclrtSetCurrentContext(context));
      HIXL_CHK_STATUS_RET(HixlCSClientConnect(handle, timeout_ms),
                          "UbClientHandler Connect failed for type:%s", CommTypeToString(type));
      return SUCCESS;
    }));
  }
  for (auto &f : futures) {
    HIXL_CHK_STATUS_RET(f.get(), "UbClientHandler Connect failed");
  }

  return FetchRemoteMem(timeout_ms);
}

Status UbClientHandler::FetchRemoteMem(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(handle_mutex_);
  for (const auto &pair : handles_) {
    auto handle = pair.second;
    CommMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    uint32_t list_num = 0;
    HIXL_CHK_STATUS_RET(HixlCSClientGetRemoteMem(handle, &remote_mem_list, &mem_tag_list, &list_num, timeout_ms));

    std::lock_guard<std::mutex> seg_lock(remote_seg_mutex_);
    for (uint32_t i = 0; i < list_num; i++) {
      MemType type = (remote_mem_list[i].type == COMM_MEM_TYPE_DEVICE) ? MEM_DEVICE : MEM_HOST;
      auto it = std::find_if(remote_segments_.begin(), remote_segments_.end(),
                             [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
      if (it != remote_segments_.end()) {
        HIXL_CHK_STATUS_RET((*it)->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr),
                                             remote_mem_list[i].size));
      } else {
        auto seg = MakeShared<Segment>(type);
        HIXL_CHK_BOOL_RET_STATUS(seg != nullptr, FAILED, "Failed to create segment");
        HIXL_CHK_STATUS_RET(seg->AddRange(reinterpret_cast<uintptr_t>(remote_mem_list[i].addr),
                                           remote_mem_list[i].size));
        remote_segments_.push_back(seg);
      }
    }
  }
  return SUCCESS;
}

Status UbClientHandler::RegisterMem(const MemInfo &mem_info) {
  const auto &mem = mem_info.mem;
  const auto type = mem_info.type;

  // 存入 local_segments_
  {
    std::lock_guard<std::mutex> lock(local_seg_mutex_);
    auto it = std::find_if(local_segments_.begin(), local_segments_.end(),
                           [type](const SegmentPtr &seg) { return seg->GetMemType() == type; });
    if (it != local_segments_.end()) {
      HIXL_CHK_STATUS_RET((*it)->AddRange(mem.addr, mem.len));
    } else {
      auto seg = MakeShared<Segment>(type);
      HIXL_CHK_BOOL_RET_STATUS(seg != nullptr, FAILED, "Failed to create segment");
      HIXL_CHK_STATUS_RET(seg->AddRange(mem.addr, mem.len));
      local_segments_.push_back(seg);
    }
  }

  // 注册到匹配的 UB CS client
  CommMem hccl_mem{};
  hccl_mem.type = (type == MemType::MEM_DEVICE) ? COMM_MEM_TYPE_DEVICE : COMM_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
  hccl_mem.size = mem.len;

  std::vector<CommType> comm_types;
  if (type == MemType::MEM_DEVICE) {
    comm_types = {CommType::COMM_TYPE_UB_D2H, CommType::COMM_TYPE_UB_D2D};
  } else {
    comm_types = {CommType::COMM_TYPE_UB_H2D, CommType::COMM_TYPE_UB_H2H};
  }

  std::lock_guard<std::mutex> lock(handle_mutex_);
  for (auto ct : comm_types) {
    auto it = handles_.find(ct);
    if (it == handles_.end()) continue;
    MemHandle mh = nullptr;
    HIXL_CHK_STATUS_RET(HixlCSClientRegMem(it->second, nullptr, &hccl_mem, &mh));
    std::lock_guard<std::mutex> ml(mem_handle_mutex_);
    mem_handles_[ct].push_back(mh);
  }
  return SUCCESS;
}

Status UbClientHandler::Transfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                  std::vector<TransferCompleteInfo> &complete_handle_list) {
  std::map<CommType, std::vector<TransferOpDesc>> table;
  HIXL_CHK_STATUS_RET(ClassifyTransfers(op_descs, table));

  std::lock_guard<std::mutex> lock(handle_mutex_);
  for (const auto &[type, descs] : table) {
    auto it = handles_.find(type);
    if (it == handles_.end()) return FAILED;
    auto handle = it->second;

    uint32_t list_num = static_cast<uint32_t>(descs.size());
    std::vector<HixlOneSideOpDesc> hixl_descs(list_num);
    for (size_t i = 0; i < list_num; i++) {
      hixl_descs[i].remote_buf = reinterpret_cast<void *>(descs[i].remote_addr);
      hixl_descs[i].local_buf = reinterpret_cast<void *>(descs[i].local_addr);
      hixl_descs[i].len = descs[i].len;
    }
    CompleteHandle complete_handle = nullptr;
    if (operation == WRITE) {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchPutAsync(handle, list_num, hixl_descs.data(), &complete_handle));
    } else {
      HIXL_CHK_STATUS_RET(HixlCSClientBatchGetAsync(handle, list_num, hixl_descs.data(), &complete_handle));
    }
    complete_handle_list.push_back({type, complete_handle});
  }
  return SUCCESS;
}

Status UbClientHandler::TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                                      uint32_t timeout_ms) {
  std::map<CommType, std::vector<TransferOpDesc>> table;
  HIXL_CHK_STATUS_RET(ClassifyTransfers(op_descs, table));

  const auto sync_start = std::chrono::steady_clock::now();
  for (const auto &[type, descs] : table) {
    uint32_t remaining_ms = 0;
    HIXL_CHK_STATUS_RET(ComputeRemainingMs(sync_start, timeout_ms, remaining_ms));

    HixlClientHandle handle = nullptr;
    {
      std::lock_guard<std::mutex> lock(handle_mutex_);
      auto it = handles_.find(type);
      if (it == handles_.end()) return FAILED;
      handle = it->second;
    }

    uint32_t list_num = static_cast<uint32_t>(descs.size());
    std::vector<void *> remote_bufs(list_num);
    std::vector<const void *> local_const(list_num);
    std::vector<void *> local_bufs(list_num);
    std::vector<const void *> remote_const(list_num);
    std::vector<uint64_t> lens(list_num);
    for (size_t i = 0; i < list_num; i++) {
      remote_bufs[i] = reinterpret_cast<void *>(descs[i].remote_addr);
      local_bufs[i] = reinterpret_cast<void *>(descs[i].local_addr);
      local_const[i] = reinterpret_cast<const void *>(descs[i].local_addr);
      remote_const[i] = reinterpret_cast<const void *>(descs[i].remote_addr);
      lens[i] = descs[i].len;
    }

    CommunicateMem com_mem{};
    com_mem.list_num = list_num;
    com_mem.len_list = lens.data();
    auto *cs = static_cast<HixlCSClient *>(handle);
    if (operation == WRITE) {
      com_mem.dst_buf_list = remote_bufs.data();
      com_mem.src_buf_list = local_const.data();
      HIXL_CHK_STATUS_RET(cs->BatchTransferSync(false, com_mem, remaining_ms));
    } else {
      com_mem.dst_buf_list = local_bufs.data();
      com_mem.src_buf_list = remote_const.data();
      HIXL_CHK_STATUS_RET(cs->BatchTransferSync(true, com_mem, remaining_ms));
    }
  }
  return SUCCESS;
}

Status UbClientHandler::QueryStatus(CommType type, CompleteHandle handle, HixlCompleteStatus &status) {
  std::lock_guard<std::mutex> lock(handle_mutex_);
  auto it = handles_.find(type);
  if (it == handles_.end()) return FAILED;
  return static_cast<Status>(HixlCSClientQueryCompleteStatus(it->second, handle, &status));
}

Status UbClientHandler::Finalize() {
  {
    std::lock_guard<std::mutex> lock(mem_handle_mutex_);
    for (auto &[type, mem_list] : mem_handles_) {
      std::lock_guard<std::mutex> hl(handle_mutex_);
      auto it = handles_.find(type);
      if (it == handles_.end()) continue;
      for (auto &mh : mem_list) {
        if (mh != nullptr) HixlCSClientUnregMem(it->second, mh);
      }
    }
    mem_handles_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(handle_mutex_);
    for (auto &pair : handles_) {
      if (pair.second != nullptr) HixlCSClientDestroy(pair.second);
    }
    handles_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(local_seg_mutex_);
    local_segments_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(remote_seg_mutex_);
    remote_segments_.clear();
  }
  return SUCCESS;
}

Status UbClientHandler::ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                                           std::map<CommType, std::vector<TransferOpDesc>> &table) {
  for (const auto &op : op_descs) {
    MemType local_mem_type;
    {
      std::lock_guard<std::mutex> lock(local_seg_mutex_);
      if (GetMemType(local_segments_, op.local_addr, op.len, local_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Local memory not registered: 0x%lx", op.local_addr);
        return PARAM_INVALID;
      }
    }
    MemType remote_mem_type;
    {
      std::lock_guard<std::mutex> lock(remote_seg_mutex_);
      if (GetMemType(remote_segments_, op.remote_addr, op.len, remote_mem_type) != SUCCESS) {
        HIXL_LOGE(PARAM_INVALID, "Remote memory not registered: 0x%lx", op.remote_addr);
        return PARAM_INVALID;
      }
    }
    CommType ct = (local_mem_type == MEM_DEVICE)
                      ? ((remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_D2D : CommType::COMM_TYPE_UB_D2H)
                      : ((remote_mem_type == MEM_DEVICE) ? CommType::COMM_TYPE_UB_H2D : CommType::COMM_TYPE_UB_H2H);
    table[ct].push_back(op);
  }
  return SUCCESS;
}

Status UbClientHandler::GetMemType(const std::vector<SegmentPtr> &segments, uintptr_t addr,
                                    size_t len, MemType &mem_type) const {
  for (const auto &seg : segments) {
    if (seg->Contains(addr, addr + len)) {
      mem_type = seg->GetMemType();
      return SUCCESS;
    }
  }
  return PARAM_INVALID;
}

}  // namespace hixl
