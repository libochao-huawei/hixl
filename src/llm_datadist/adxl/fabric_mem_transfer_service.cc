/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include "fabric_mem_transfer_service.h"
#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <vector>
#include "adxl/adxl_checker.h"
#include "common/def_types.h"
#include "common/llm_scope_guard.h"
#include "common/llm_log.h"
#include "statistic_manager.h"

namespace adxl {
namespace {
constexpr uint64_t kMillisToMicros = 1000;
constexpr size_t kTaskStreamNum = 4;
}  // namespace
Status FabricMemTransferService::Initialize(size_t max_stream_num) {
  ADXL_CHK_ACL_RET(rtGetDevice(&device_id_));
  LLMLOGI("Get device id:%d", device_id_);
  max_stream_num_ = max_stream_num;
  return SUCCESS;
}

void FabricMemTransferService::Finalize() {
  {
    std::lock_guard<std::mutex> async_req_lock(async_req_mutex_);
    for (auto &req_2_record : req_2_async_record_) {
      DestroyAsyncResources(req_2_record.second.async_resources);
    }
    req_2_async_record_.clear();
  }
  {
    std::lock_guard<std::mutex> async_req_lock(channel_2_req_mutex_);
    channel_2_req_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(stream_pool_mutex_);
    for (auto &stream_stat : stream_pool_) {
      if (stream_stat.first != nullptr) {
        LLM_CHK_ACL(rtStreamDestroy(stream_stat.first));
      }
    }
    stream_pool_.clear();
  }
}

Status FabricMemTransferService::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  rtDrvMemHandle pa_handle;
  ADXL_CHK_ACL_RET(rtMemRetainAllocationHandle(llm::ValueToPtr(mem.addr), &pa_handle));
  rtDrvMemFabricHandle share_handle = {};
  ADXL_CHK_ACL_RET(rtMemExportToShareableHandleV2(pa_handle, RT_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U, &share_handle));
  share_handles_[pa_handle] = ShareHandleInfo{mem.addr, mem.len, share_handle};
  mem_handle = pa_handle;
  LLMLOGI("Export suc, mem type:%d, mem addr:%lu.", type, mem.addr);
  return SUCCESS;
}

Status FabricMemTransferService::DeregisterMem(MemHandle mem_handle) {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  auto it = share_handles_.find(mem_handle);
  if (it != share_handles_.end()) {
    share_handles_.erase(it);
  }
  return SUCCESS;
}

Status FabricMemTransferService::Transfer(const ChannelPtr &channel, TransferOp operation,
                                          const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis) {
  const auto start = std::chrono::steady_clock::now();
  uint64_t timeout = timeout_in_millis * kMillisToMicros;
  std::vector<rtStream_t> streams;
  streams.reserve(kTaskStreamNum);
  ADXL_CHK_STATUS_RET(TryGetStream(streams, timeout), "Failed to get stream.");
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &streams]() -> void {
                          std::lock_guard<std::mutex> lock(stream_pool_mutex_);
                          for (auto &stream : streams) {
                            LLM_CHK_ACL(rtStreamDestroy(stream));
                            auto it = stream_pool_.find(stream);
                            if (it != stream_pool_.end()) {
                              stream_pool_.erase(it);
                            }
                          }
                        }));
  auto real_copy_start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(DoTransfer(streams, channel, operation, op_descs, real_copy_start), "Failed to transfer.");
  for (auto &stream : streams) {
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Transfer timeout.");
    uint64_t stream_timeout = (timeout - time_cost) / kMillisToMicros;
    ADXL_CHK_BOOL_RET_STATUS(stream_timeout > 0, TIMEOUT, "Transfer timeout.");
    ADXL_CHK_ACL_RET(rtStreamSynchronizeWithTimeout(stream, stream_timeout));
  }
  uint64_t real_copy_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - real_copy_start).count();
  StatisticManager::GetInstance().UpdateFabricMemRealCopyCost(channel->GetChannelId(), real_copy_cost);
  LLM_DISMISS_GUARD(fail_guard);
  ReleaseStreams(streams);
  uint64_t transfer_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  StatisticManager::GetInstance().UpdateFabricMemTransferCost(channel->GetChannelId(), transfer_cost);
  LLMLOGI("Transfer time cost:%lu us", transfer_cost);
  return SUCCESS;
}

Status FabricMemTransferService::TransferAsync(const ChannelPtr &channel, TransferOp operation,
                                               const std::vector<TransferOpDesc> &op_descs, TransferReq &req) {
  std::vector<rtStream_t> streams;
  streams.reserve(kTaskStreamNum);
  ADXL_CHK_STATUS_RET(TryGetStreamOnce(streams), "Failed to get stream.");
  auto real_copy_start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(DoTransfer(streams, channel, operation, op_descs, real_copy_start), "Failed to transfer.");
  std::vector<AsyncResource> async_resources;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &async_resources, &streams]() {
                          for (auto &async_resource : async_resources) {
                            LLM_CHK_ACL(rtEventDestroy(async_resource.second));
                          }
                          std::lock_guard<std::mutex> lock(stream_pool_mutex_);
                          for (auto &stream : streams) {
                            LLM_CHK_ACL(rtStreamDestroy(stream));
                            auto it = stream_pool_.find(stream);
                            if (it != stream_pool_.end()) {
                              stream_pool_.erase(it);
                            }
                          }
                        }));
  for (auto &stream : streams) {
    rtEvent_t event = nullptr;
    LLM_CHK_ACL_RET(rtEventCreate(&event));
    async_resources.emplace_back(stream, event);
    LLM_CHK_ACL_RET(rtEventRecord(event, stream));
  }
  {
    std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
    channel_2_req_[channel->GetChannelId()].emplace(llm::PtrToValue(req));
  }
  {
    std::lock_guard<std::mutex> lock(async_req_mutex_);
    req_2_async_record_[llm::PtrToValue(req)] = AsyncRecord{async_resources, real_copy_start};
  }
  LLMLOGI("Transfer async call end, channel:%s, req:%lu.", channel->GetChannelId().c_str(), llm::PtrToValue(req));
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

Status FabricMemTransferService::GetTransferStatus(const ChannelPtr &channel, const TransferReq &req,
                                                   TransferStatus &status) {
  std::lock_guard<std::mutex> lock(async_req_mutex_);
  auto req_id = llm::PtrToValue(req);
  auto async_record_it = req_2_async_record_.find(req_id);
  ADXL_CHK_BOOL_RET_STATUS(async_record_it != req_2_async_record_.end(), FAILED, "Request:%lu not found.", req_id);
  const auto &async_record = async_record_it->second;
  const auto &async_resources = async_record.async_resources;
  LLM_DISMISSABLE_GUARD(fail_guard, ([this, &channel, &async_record_it, &async_resources]() {
                          DestroyAsyncResources(async_resources);
                          RemoveChannelReqRelation(channel->GetChannelId(), async_record_it->first);
                          req_2_async_record_.erase(async_record_it);
                        }));
  bool completed = true;
  for (auto &async_resource : async_resources) {
    rtEventStatus_t event_status{};
    auto ret = rtEventQueryStatus(async_resource.second, &event_status);
    if (ret != RT_ERROR_NONE) {
      LLMLOGE(FAILED, "Call rtEventQueryStatus failed for req:%llu, ret:%d.", req_id, ret);
      status = TransferStatus::FAILED;
      return FAILED;
    }
    if (event_status != RT_EVENT_RECORDED) {
      LLMLOGD("Transfer async request not yet completed, req:%llu.", req_id);
      status = TransferStatus::WAITING;
      completed = false;
    }
  }
  if (completed) {
    // call sync in case error happens
    status = TransferStatus::COMPLETED;
    for (auto &async_resource : async_resources) {
      auto ret = rtStreamSynchronize(async_resource.first);
      if (ret != RT_ERROR_NONE) {
        LLMLOGE(FAILED, "Call rtStreamSynchronize failed for req:%lu, ret:%d.", req_id, ret);
        status = TransferStatus::FAILED;
        continue;
      }
    }
    if (status == TransferStatus::FAILED) {
      return FAILED;
    }
    // release streams
    std::vector<rtStream_t> streams;
    streams.reserve(async_resources.size());
    for (auto &async_resource : async_resources) {
      streams.emplace_back(async_resource.first);
    }
    ReleaseStreams(streams);
    RemoveChannelReqRelation(channel->GetChannelId(), req_id);
    uint64_t transfer_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                                   async_record.real_start)
                                 .count();
    StatisticManager::GetInstance().UpdateFabricMemTransferCost(channel->GetChannelId(), transfer_cost);
    req_2_async_record_.erase(async_record_it);
    LLMLOGI("Transfer async request completed, channel:%s, req:%lu.", channel->GetChannelId().c_str(), req_id);
  }
  LLM_DISMISS_GUARD(fail_guard);
  return SUCCESS;
}

void FabricMemTransferService::RemoveChannel(const std::string &channel_id) {
  std::lock_guard<std::mutex> lock(channel_2_req_mutex_);
  auto it = channel_2_req_.find(channel_id);
  if (it == channel_2_req_.end()) {
    return;
  }
  std::lock_guard<std::mutex> async_req_lock(async_req_mutex_);
  // destroy all async resources related to this channel
  for (auto &req_id : it->second) {
    LLMLOGI("Destroy async resources, channel:%s, req:%lu.", channel_id.c_str(), req_id);
    auto async_record_it = req_2_async_record_.find(req_id);
    if (async_record_it == req_2_async_record_.end()) {
      continue;
    }
    DestroyAsyncResources(async_record_it->second.async_resources);
    // remove async record
    req_2_async_record_.erase(async_record_it);
  }
  // remove all relations of channel
  channel_2_req_.erase(it);
}

void FabricMemTransferService::RemoveChannelReqRelation(const std::string &channel_id, const uint64_t req_id) {
  std::lock_guard<std::mutex> channel_2_req_lock(channel_2_req_mutex_);
  auto channel_2_req_it = channel_2_req_.find(channel_id);
  if (channel_2_req_it != channel_2_req_.end()) {
    auto req_id_it = std::find(channel_2_req_it->second.begin(), channel_2_req_it->second.end(), req_id);
    if (req_id_it != channel_2_req_it->second.end()) {
      channel_2_req_it->second.erase(req_id_it);
    }
  }
}

void FabricMemTransferService::DestroyAsyncResources(const std::vector<AsyncResource> &async_resources) {
  std::lock_guard<std::mutex> stream_lock(stream_pool_mutex_);
  for (auto &async_resource : async_resources) {
    LLM_CHK_ACL(rtEventDestroy(async_resource.second));
    auto &stream = async_resource.first;
    LLM_CHK_ACL(rtStreamDestroy(stream));
    auto stream_it = stream_pool_.find(stream);
    if (stream_it != stream_pool_.end()) {
      stream_pool_.erase(stream_it);
    }
  }
}

Status FabricMemTransferService::TryGetStream(std::vector<rtStream_t> &streams, uint64_t timeout) {
  auto start = std::chrono::steady_clock::now();
  streams.reserve(kTaskStreamNum);
  while (true) {
    ADXL_CHK_BOOL_RET_SPECIAL_STATUS(TryGetStreamOnce(streams) == SUCCESS, SUCCESS, "Success to get stream.");
    uint64_t time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    ADXL_CHK_BOOL_RET_STATUS(time_cost < timeout, TIMEOUT, "Get stream timeout.");
  }
}

Status FabricMemTransferService::TryGetStreamOnce(std::vector<rtStream_t> &streams) {
  {
    std::lock_guard<std::mutex> lock(stream_pool_mutex_);
    for (auto &stream_stat : stream_pool_) {
      if (stream_stat.second) {
        stream_stat.second = false;
        streams.emplace_back(stream_stat.first);
        if (streams.size() >= kTaskStreamNum) {
          return SUCCESS;
        }
      }
    }
  }
  while (streams.size() < kTaskStreamNum) {
    if (stream_pool_.size() >= max_stream_num_) {
      LLMEVENT("Stream pool is full, current stream pool size:%zu.", stream_pool_.size());
      ReleaseStreams(streams);
      return FAILED;
    }
    rtStream_t stream = nullptr;
    ADXL_CHK_ACL_RET(
        rtStreamCreateWithFlags(&stream, RT_STREAM_PRIORITY_DEFAULT, RT_STREAM_FAST_LAUNCH | RT_STREAM_FAST_SYNC));
    streams.emplace_back(stream);
    LLMEVENT("Create new stream, current stream pool size:%zu.", stream_pool_.size());
  }
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  for (const auto &stream : streams) {
    stream_pool_[stream] = false;
  }
  return SUCCESS;
}

void FabricMemTransferService::ReleaseStreams(std::vector<rtStream_t> &streams) {
  std::lock_guard<std::mutex> lock(stream_pool_mutex_);
  for (auto &stream : streams) {
    auto it = stream_pool_.find(stream);
    if (it != stream_pool_.end()) {
      it->second = true;
    }
  }
}

Status FabricMemTransferService::DoTransfer(const std::vector<rtStream_t> &streams, const ChannelPtr &channel,
                                            TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                                            std::chrono::steady_clock::time_point &start) {
  std::vector<TransferOpDesc> new_op_descs;
  new_op_descs.reserve(op_descs.size());
  // Get imported memory info from channel
  auto &new_va_to_old_va = channel->GetNewVaToOldVa();
  for (size_t i = 0; i < op_descs.size(); ++i) {
    const auto &op = op_descs[i];
    bool found = false;
    for (const auto &[new_va, info] : new_va_to_old_va) {
      if ((op.remote_addr >= info.va_addr) && (op.remote_addr < info.va_addr + info.len)) {
        if ((op.remote_addr + op.len) > (info.va_addr + info.len)) {
          LLMLOGE(PARAM_INVALID,
                  "Remote address out of range. remote_addr: %lu, len: %lu, segment start: %lu, segment len: %lu",
                  op.remote_addr, op.len, info.va_addr, info.len);
          return PARAM_INVALID;
        }
        uintptr_t offset = op.remote_addr - info.va_addr;
        uintptr_t new_addr = new_va + offset;
        TransferOpDesc new_op = op;
        new_op.remote_addr = new_addr;
        LLMLOGI("Old remote_addr: %lu, new remote_addr: %lu, len: %lu.", op.remote_addr, new_op.remote_addr,
                new_op.len);
        new_op_descs.push_back(new_op);
        found = true;
        break;
      }
    }
    if (!found) {
      LLMLOGE(PARAM_INVALID, "Remote address of number %zu op_desc not found in registered segments. remote_addr: %lu",
              i, op.remote_addr);
      return PARAM_INVALID;
    }
  }
  start = std::chrono::steady_clock::now();
  ADXL_CHK_STATUS_RET(ProcessCopyWithAsync(streams, operation, new_op_descs), "Failed to copy.");
  return SUCCESS;
}

Status FabricMemTransferService::ProcessCopyWithAsync(const std::vector<rtStream_t> &streams, TransferOp operation,
                                                      const std::vector<TransferOpDesc> &op_descs) {
  for (size_t i = 0; i < op_descs.size(); ++i) {
    const auto &op = op_descs[i];
    auto &stream = streams[i % (streams.size())];
    if (operation == TransferOp::WRITE) {
      ADXL_CHK_ACL_RET(rtMemcpyAsync(llm::ValueToPtr(op.remote_addr), op.len, llm::ValueToPtr(op.local_addr), op.len,
                                     RT_MEMCPY_DEVICE_TO_DEVICE, stream));
    } else if (operation == TransferOp::READ) {
      ADXL_CHK_ACL_RET(rtMemcpyAsync(llm::ValueToPtr(op.local_addr), op.len, llm::ValueToPtr(op.remote_addr), op.len,
                                     RT_MEMCPY_DEVICE_TO_DEVICE, stream));
    }
  }
  return SUCCESS;
}

std::vector<ShareHandleInfo> FabricMemTransferService::GetShareHandles() {
  std::lock_guard<std::mutex> lock(share_handle_mutex_);
  std::vector<ShareHandleInfo> share_handles;
  share_handles.reserve(share_handles_.size());
  for (auto &share_handle : share_handles_) {
    share_handles.push_back(share_handle.second);
  }
  return share_handles;
}

Status FabricMemTransferService::ImportMem(const ChannelPtr &channel,
                                           const std::vector<ShareHandleInfo> &remote_share_handles) const {
  return channel->ImportMem(remote_share_handles, device_id_);
}

Status FabricMemTransferService::SetPid(int32_t pid) {
  for (auto &share_handle : share_handles_) {
    ADXL_CHK_ACL_RET(
        rtMemSetPidToShareableHandleV2(&share_handle.second.share_handle, RT_MEM_SHARE_HANDLE_TYPE_FABRIC, &pid, 1));
  }
  return SUCCESS;
}

}  // namespace adxl
