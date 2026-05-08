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
#include <string>
#include <vector>
#include <chrono>
#include "common/hixl_log.h"
#include "common/hixl_checker.h"
#include "proxy/hcomm_proxy.h"
#include "hixl/hixl.h"

namespace hixl {
namespace {
constexpr uint32_t kMaxBatchSizeOnStack = 1024;  // 栈上预分配的最大批量大小

int32_t TransferWithBatch(bool is_read, HixlOneSideOpParam *param) {
  // 对于小批量，使用栈数组避免动态分配开销
  if (param->list_num <= kMaxBatchSizeOnStack) {
    HcommBatchTransferDesc descs[kMaxBatchSizeOnStack];
    for (uint32_t i = 0; i < param->list_num; i++) {
      descs[i].transType = is_read ? HCOMM_TRANSFER_TYPE_READ : HCOMM_TRANSFER_TYPE_WRITE;
      if (is_read) {
        descs[i].read.len = param->len_list[i];
        descs[i].read.dst = param->dst_buf_addr_list[i];
        descs[i].read.src = param->src_buf_addr_list[i];
      } else {
        descs[i].write.len = param->len_list[i];
        descs[i].write.dst = param->dst_buf_addr_list[i];
        descs[i].write.src = param->src_buf_addr_list[i];
      }
    }
    return HcommProxy::BatchTransferOnThread(param->thread, param->channel, descs, param->list_num);
  }
  // 大批量使用动态分配（这种情况较少）
  std::vector<HcommBatchTransferDesc> descs(param->list_num);
  for (uint32_t i = 0; i < param->list_num; i++) {
    descs[i].transType = is_read ? HCOMM_TRANSFER_TYPE_READ : HCOMM_TRANSFER_TYPE_WRITE;
    if (is_read) {
      descs[i].read.len = param->len_list[i];
      descs[i].read.dst = param->dst_buf_addr_list[i];
      descs[i].read.src = param->src_buf_addr_list[i];
    } else {
      descs[i].write.len = param->len_list[i];
      descs[i].write.dst = param->dst_buf_addr_list[i];
      descs[i].write.src = param->src_buf_addr_list[i];
    }
  }
  return HcommProxy::BatchTransferOnThread(param->thread, param->channel, descs.data(), param->list_num);
}

uint32_t TransferWithSingle(bool is_read, HixlOneSideOpParam *param) {
  const auto total_start = std::chrono::steady_clock::now();
  uint64_t total_transfer_us = 0;
  if (is_read) {
    // 批量提交读任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, param->dst_buf_addr_list[i], i,
          param->src_buf_addr_list[i], i, param->len_list[i]);
      const auto op_start = std::chrono::steady_clock::now();
      int32_t ret = HcommProxy::ReadOnThread(param->thread, param->channel, param->dst_buf_addr_list[i],
                                             param->src_buf_addr_list[i], param->len_list[i]);
      const auto op_end = std::chrono::steady_clock::now();
      total_transfer_us += std::chrono::duration_cast<std::chrono::microseconds>(op_end - op_start).count();
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "HcommReadOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u, "
                  "ret is %d.",
                  param->dst_buf_addr_list[i], param->src_buf_addr_list[i], param->len_list[i], ret);
        return FAILED;
      }
    }
  } else {
    // 批量提交写任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommWriteOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, param->dst_buf_addr_list[i], i,
          param->src_buf_addr_list[i], i, param->len_list[i]);
      const auto op_start = std::chrono::steady_clock::now();
      int32_t ret = HcommProxy::WriteOnThread(param->thread, param->channel, param->dst_buf_addr_list[i],
                                              param->src_buf_addr_list[i], param->len_list[i]);
      const auto op_end = std::chrono::steady_clock::now();
      total_transfer_us += std::chrono::duration_cast<std::chrono::microseconds>(op_end - op_start).count();
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "HcommWriteOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, "
                  "buf_len:%u, ret is %d.",
                  param->dst_buf_addr_list[i], param->src_buf_addr_list[i], param->len_list[i], ret);
        return FAILED;
      }
    }
  }
  const auto total_end = std::chrono::steady_clock::now();
  const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
  HIXL_LOGE(SUCCESS, "[TransferWithSingle] timing(us): total=%ld transfer=%ld is_read=%d list_num=%u",
            total_us, total_transfer_us, is_read, param->list_num);
  return SUCCESS;
}

uint32_t HixlBatchTransferTask(bool is_read, HixlOneSideOpParam *param) {
  // 先尝试批量接口
  int32_t batch_ret = TransferWithBatch(is_read, param);
  if (batch_ret == HCCL_E_NOT_SUPPORT) {
    // fallback 到逐个调用
    HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread not supported, fallback to single calls");
    return TransferWithSingle(is_read, param);
  }
  if (batch_ret != 0) {
    HIXL_LOGE(FAILED, "HcommBatchTransferOnThread failed, ret=%d", batch_ret);
    return FAILED;
  }
  HIXL_LOGI("[HixlBatchTransfer] HcommBatchTransferOnThread success");
  return SUCCESS;
}

uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  const auto total_start = std::chrono::steady_clock::now();
  HIXL_LOGI("[HixlBatchPutAndGet] HixlBatchTransfer %s start.", is_read ? "read" : "write");
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
    return PARAM_INVALID;
  }

  const auto batch_start_start = std::chrono::steady_clock::now();
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeStart start");
  constexpr const char *kBatchTag = "HixlKernel";
  int32_t ret = HcommProxy::BatchModeStart(kBatchTag);
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeStart end");
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HcommBatchModeStart failed, ret is %d", ret);
  const auto batch_start_end = std::chrono::steady_clock::now();
  const auto batch_start_us = std::chrono::duration_cast<std::chrono::microseconds>(batch_start_end - batch_start_start).count();

  const auto transfer_start = std::chrono::steady_clock::now();
  ret = HixlBatchTransferTask(is_read, param);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HixlBatchTransferTask failed, ret is %d", ret);
  const auto transfer_end = std::chrono::steady_clock::now();
  const auto transfer_us = std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - transfer_start).count();

  const auto fence_start = std::chrono::steady_clock::now();
  ret = HcommProxy::ChannelFenceOnThread(param->thread, param->channel);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HcommChannelFenceOnThread failed, ret is %d", ret);
  const auto fence_end = std::chrono::steady_clock::now();
  const auto fence_us = std::chrono::duration_cast<std::chrono::microseconds>(fence_end - fence_start).count();

  const auto flag_start = std::chrono::steady_clock::now();
  HIXL_LOGI(
      "[HixlBatchPutAndGet] HcommReadOnThread start to read remote flag, param->flag_size=%u, param->local_flag=%lu, "
      "param->remote_flag=%lu",
      param->flag_size, param->local_flag_addr, param->remote_flag_addr);
  ret = HcommProxy::ReadOnThread(
      param->thread, param->channel, reinterpret_cast<void *>(static_cast<uintptr_t>(param->local_flag_addr)),
      reinterpret_cast<void *>(static_cast<uintptr_t>(param->remote_flag_addr)), param->flag_size);
  HIXL_CHK_BOOL_RET_STATUS(
      ret == 0, FAILED,
      "[HixlBatchPutAndGet] Remote flag read failed. The address information is as follows:dst_buf:%lu, "
      "scr_buf:%lu, buf_len:%u, ret is %d.",
      param->local_flag_addr, param->remote_flag_addr, param->flag_size, ret);
  const auto flag_end = std::chrono::steady_clock::now();
  const auto flag_us = std::chrono::duration_cast<std::chrono::microseconds>(flag_end - flag_start).count();

  const auto batch_end_start = std::chrono::steady_clock::now();
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeEnd start");
  ret = HcommProxy::BatchModeEnd(kBatchTag);
  HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED, "[HixlBatchPutAndGet] HcommBatchModeEnd failed, ret is %d", ret);
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeEnd end");
  const auto batch_end_end = std::chrono::steady_clock::now();
  const auto batch_end_us = std::chrono::duration_cast<std::chrono::microseconds>(batch_end_end - batch_end_start).count();

  const auto total_end = std::chrono::steady_clock::now();
  const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
  HIXL_LOGE(SUCCESS, "[HixlBatchTransfer] timing(us): total=%ld batch_start=%ld transfer=%ld fence=%ld flag=%ld batch_end=%ld is_read=%d list_num=%u",
            total_us, batch_start_us, transfer_us, fence_us, flag_us, batch_end_us, is_read, param->list_num);
  return SUCCESS;
}
}  // namespace
}  // namespace hixl
extern "C" {
uint32_t HixlBatchPut(HixlOneSideOpParam *param) {
  uint32_t ret = hixl::HixlBatchTransfer(false, param);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchPut] HixlBatchPut failed, ret is %u", ret);
    return hixl::FAILED;
  }
  return ret;
}

uint32_t HixlBatchGet(HixlOneSideOpParam *param) {
  uint32_t ret = hixl::HixlBatchTransfer(true, param);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchGet] HixlBatchGet failed, ret is %u", ret);
    return hixl::FAILED;
  }
  return ret;
}
}  // extern "C"