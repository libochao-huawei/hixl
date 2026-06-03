/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "hixl_batch_transfer.h"
#include <chrono>
#include <string>
#include <vector>
#include "common/hixl_log.h"
#include "common/hixl_checker.h"
#include "proxy/hcomm_proxy.h"
#include "hixl/hixl.h"

namespace hixl {
namespace {
constexpr uint32_t kMaxBatchSize = 16584;

int32_t TransferWithBatch(bool is_read, HixlOneSideOpParam *param) {
  bool perf = param->enable_perf;
  auto t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(static_cast<uintptr_t>(param->op_desc_list_addr));
  std::vector<HcommBatchTransferDesc> descs(param->list_num);
  for (uint32_t i = 0; i < param->list_num; i++) {
    descs[i].transType = is_read ? HCOMM_TRANSFER_TYPE_READ : HCOMM_TRANSFER_TYPE_WRITE;
    if (is_read) {
      descs[i].transferInfo.read.len = op_list[i].len;
      descs[i].transferInfo.read.dst = op_list[i].local_buf;
      descs[i].transferInfo.read.src = op_list[i].remote_buf;
    } else {
      descs[i].transferInfo.write.len = op_list[i].len;
      descs[i].transferInfo.write.dst = op_list[i].remote_buf;
      descs[i].transferInfo.write.src = op_list[i].local_buf;
    }
  }
  auto t1 = std::chrono::steady_clock::now();

  int32_t ret = 0;
  uint32_t offset = 0;
  while (offset < param->list_num) {
    uint32_t batch_size = std::min(kMaxBatchSize, param->list_num - offset);
    ret = HcommProxy::BatchTransferOnThread(param->thread, param->channel, descs.data() + offset, batch_size);
    if (ret != 0) {
      if (ret == HCCL_E_NOT_SUPPORT) {
        HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread not supported.");
        return HCCL_E_NOT_SUPPORT;
      }
      HIXL_LOGE(FAILED, "[HixlBatchTransfer] BatchTransferOnThread failed at offset %u, batch_size %u, ret=%d", offset,
                batch_size, ret);
      return ret;
    }
    offset += batch_size;
  }
  if (perf) {
    auto t2 = std::chrono::steady_clock::now();
    auto cost_vector_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    HIXL_EVENT("[HixlBatchTransfer] PerfStat construct param=%luus, BatchTransferOnThread=%luus", cost_vector_us,
               static_cast<uint64_t>(cost_us));
  }
  return ret;
}

uint32_t TransferWithSingle(bool is_read, HixlOneSideOpParam *param) {
  bool perf = param->enable_perf;
  auto t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(static_cast<uintptr_t>(param->op_desc_list_addr));
  if (is_read) {
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, op_list[i].local_buf, i, op_list[i].remote_buf, i,
          op_list[i].len);
      int32_t ret = HcommProxy::ReadOnThread(param->thread, param->channel, op_list[i].local_buf, op_list[i].remote_buf,
                                             op_list[i].len);
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "HcommReadOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u, "
                  "ret is %d.",
                  op_list[i].local_buf, op_list[i].remote_buf, op_list[i].len, ret);
        return FAILED;
      }
    }
  } else {
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommWriteOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, op_list[i].remote_buf, i, op_list[i].local_buf, i,
          op_list[i].len);
      int32_t ret = HcommProxy::WriteOnThread(param->thread, param->channel, op_list[i].remote_buf,
                                              op_list[i].local_buf, op_list[i].len);
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "HcommWriteOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, "
                  "buf_len:%u, ret is %d.",
                  op_list[i].remote_buf, op_list[i].local_buf, op_list[i].len, ret);
        return FAILED;
      }
    }
  }
  if (perf) {
    auto t1 = std::chrono::steady_clock::now();
    auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    HIXL_EVENT("[HixlBatchTransfer] PerfStat TransferWithSingle=%luus", static_cast<uint64_t>(cost_us));
  }
  return SUCCESS;
}

uint32_t HixlBatchTransferTask(bool is_read, HixlOneSideOpParam *param) {
  bool perf = param->enable_perf;
  auto t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  int32_t batch_ret = TransferWithBatch(is_read, param);
  if (batch_ret == HCCL_E_NOT_SUPPORT) {
    HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread not supported, fallback to single calls");
    uint32_t single_ret = TransferWithSingle(is_read, param);
    if (perf) {
      auto t1 = std::chrono::steady_clock::now();
      auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
      HIXL_EVENT("[HixlBatchTransfer] PerfStat BatchTransferTask=%luus (fallback)", static_cast<uint64_t>(cost_us));
    }
    return single_ret;
  }
  if (batch_ret != 0) {
    HIXL_LOGE(FAILED, "HcommBatchTransferOnThread failed, ret=%d", batch_ret);
    return FAILED;
  }
  HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread success");
  if (perf) {
    auto t1 = std::chrono::steady_clock::now();
    auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    HIXL_EVENT("[HixlBatchTransfer] PerfStat BatchTransferTask=%luus (batch)", static_cast<uint64_t>(cost_us));
  }
  return SUCCESS;
}

uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  HIXL_LOGI("[HixlBatchPutAndGet] HixlBatchTransfer %s start.", is_read ? "read" : "write");
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
    return PARAM_INVALID;
  }

  bool perf = param->enable_perf;
  auto t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  auto t1 = t0;
  auto t2 = t0;
  auto t3 = t0;
  auto t4 = t0;
  auto t5 = t0;

  constexpr const char *kBatchTag = "HixlKernel";
  int32_t ret = HcommProxy::BatchModeStart(kBatchTag);
  t1 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  if (ret != 0) {
    HIXL_REPORT_ERR_MSG("E19999", "[HixlBatchPutAndGet] HcommBatchModeStart failed, ret is %d", ret);
    HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] HcommBatchModeStart failed, ret is %d", ret);
    return FAILED;
  }

  ret = HixlBatchTransferTask(is_read, param);
  t2 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  if (ret != 0) {
    HIXL_REPORT_ERR_MSG("E19999", "[HixlBatchPutAndGet] HixlBatchTransferTask failed, ret is %d", ret);
    HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] HixlBatchTransferTask failed, ret is %d", ret);
    return FAILED;
  }

  ret = HcommProxy::ChannelFenceOnThread(param->thread, param->channel);
  t3 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  if (ret != 0) {
    HIXL_REPORT_ERR_MSG("E19999", "[HixlBatchPutAndGet] HcommChannelFenceOnThread failed, ret is %d", ret);
    HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] HcommChannelFenceOnThread failed, ret is %d", ret);
    return FAILED;
  }

  t4 = t3;
  HIXL_LOGI("[HixlBatchPutAndGet] HixlBatchTransfer use_notify_record=%d.", param->use_notify_record);
  if (param->remote_flag_addr != 0) {
    if (!param->use_notify_record) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start to read remote flag, flag_size=%u, "
          "local_flag=%lu, remote_flag=%lu",
          param->flag_size, param->local_flag_addr, param->remote_flag_addr);
      ret = HcommProxy::ReadOnThread(
          param->thread, param->channel, reinterpret_cast<void *>(static_cast<uintptr_t>(param->local_flag_addr)),
          reinterpret_cast<void *>(static_cast<uintptr_t>(param->remote_flag_addr)), param->flag_size);
      t4 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
      if (ret != 0) {
        HIXL_REPORT_ERR_MSG("E19999", "[HixlBatchPutAndGet] Remote flag read failed. dst:%lu, src:%lu, len:%u, ret=%d.",
                            param->local_flag_addr, param->remote_flag_addr, param->flag_size, ret);
        HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] Remote flag read failed. dst:%lu, src:%lu, len:%u, ret=%d.",
                  param->local_flag_addr, param->remote_flag_addr, param->flag_size, ret);
        return FAILED;
      }
    } else {
      HIXL_LOGI("[HixlBatchPutAndGet] aclrtNotifyRecordOnThread start to read remote flag, thread[%lu], notify_id[%u]",
                param->thread, param->notify_id);
      ret = HcommProxy::aclrtNotifyRecordOnThread(param->thread, param->notify_id);
      t4 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
      if (ret != 0) {
        HIXL_REPORT_ERR_MSG("E19999",
                            "[HixlBatchPutAndGet] aclrtNotifyRecordOnThread start to read remote flag failed, "
                            "thread[%lu], notify_id[%u]",
                            param->thread, param->notify_id);
        HIXL_LOGE(FAILED,
                  "[HixlBatchPutAndGet] aclrtNotifyRecordOnThread start to read remote flag failed, thread[%lu], "
                  "notify_id[%u]",
                  param->thread, param->notify_id);
        return FAILED;
      }
    }
  }

  ret = HcommProxy::BatchModeEnd(kBatchTag);
  t5 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  if (ret != 0) {
    HIXL_REPORT_ERR_MSG("E19999", "[HixlBatchPutAndGet] HcommBatchModeEnd failed, ret is %d", ret);
    HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] HcommBatchModeEnd failed, ret is %d", ret);
    return FAILED;
  }

  if (perf) {
    auto batch_start_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto task_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    auto fence_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    auto flag_us = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
    auto batch_end_us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t0).count();
    HIXL_EVENT(
        "[HixlBatchTransfer] PerfStat Detail: BatchModeStart=%luus, TransferTask=%luus, "
        "ChannelFence=%luus, FlagNotify=%luus, BatchModeEnd=%luus, Total=%luus",
        static_cast<uint64_t>(batch_start_us), static_cast<uint64_t>(task_us), static_cast<uint64_t>(fence_us),
        static_cast<uint64_t>(flag_us), static_cast<uint64_t>(batch_end_us), static_cast<uint64_t>(total_us));
  }

  return SUCCESS;
}
}  // namespace
}  // namespace hixl
extern "C" {
uint32_t HixlBatchPut(HixlOneSideOpParam *param) {
  bool perf = param->enable_perf;
  auto t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  uint32_t ret = hixl::HixlBatchTransfer(false, param);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchPut] HixlBatchPut failed, ret is %u", ret);
    return hixl::FAILED;
  }
  if (perf) {
    auto t1 = std::chrono::steady_clock::now();
    auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    HIXL_EVENT("[HixlBatchPut] PerfStat HixlBatchPut=%luus", static_cast<uint64_t>(cost_us));
  }
  return ret;
}

uint32_t HixlBatchGet(HixlOneSideOpParam *param) {
  bool perf = param->enable_perf;
  auto t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  uint32_t ret = hixl::HixlBatchTransfer(true, param);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchGet] HixlBatchGet failed, ret is %u", ret);
    return hixl::FAILED;
  }
  if (perf) {
    auto t1 = std::chrono::steady_clock::now();
    auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    HIXL_EVENT("[HixlBatchGet] PerfStat HixlBatchGet=%luus", static_cast<uint64_t>(cost_us));
  }
  return ret;
}
}  // extern "C"