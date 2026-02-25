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
#include "common/hixl_log.h"
#include "hixl/hixl.h"

namespace {
uint32_t HixlBatchTransferTask(bool is_read, HixlOneSideOpParam *param) {
  if (is_read) {
    // 批量提交读任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, param->dst_buf_addr_list[i], i, param->src_buf_addr_list[i], i,
          param->len_list[i]);
      int32_t ret = HcommReadOnThread(param->thread, param->channel, param->dst_buf_addr_list[i], param->src_buf_addr_list[i],
                                      param->len_list[i]);
      if (ret != 0) {
        HIXL_LOGE(hixl::FAILED,
                  "HcommReadOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u, "
                  "ret is %d.",
                  param->dst_buf_addr_list[i], param->src_buf_addr_list[i], param->len_list[i], ret);
        return hixl::FAILED;
      }
    }
  } else {
    // 批量提交写任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%lu",
          param->list_num, i, param->thread, param->channel, i, param->dst_buf_addr_list[i], i, param->src_buf_addr_list[i], i,
          param->len_list[i]);
      int32_t ret = HcommWriteOnThread(param->thread, param->channel, param->dst_buf_addr_list[i], param->src_buf_addr_list[i],
                                       param->len_list[i]);
      if (ret != 0) {
        HIXL_LOGE(hixl::FAILED,
                  "HcommWriteOnThread failed. The address information is as follows:dst_buf:%p, scr_buf:%p, "
                  "buf_len:%u, ret is %d.",
                  param->dst_buf_addr_list[i], param->src_buf_addr_list[i], param->len_list[i], ret);
        return hixl::FAILED;
      }
    }
  }
  return hixl::SUCCESS;
}

uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  HIXL_LOGI("[HixlBatchPutAndGet] HixlBatchTransfer %s start.", is_read ? "read" : "write");
  if (param == nullptr) {
    HIXL_LOGE(hixl::PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
    return hixl::PARAM_INVALID;
  }
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeStart start");
  constexpr const char *kBatchTag = "HixlKernel";
  int32_t ret = HcommBatchModeStart(kBatchTag);
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeStart end");
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchPutAndGet] HcommBatchModeStart faild, ret is %d", ret);
    return hixl::FAILED;
  }
  ret = HixlBatchTransferTask(is_read, param);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchPutAndGet] HixlBatchTransferTask faild, ret is %d", ret);
    return hixl::FAILED;
  }
  ret = HcommChannelFenceOnThread(param->thread, param->channel);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchPutAndGet] HcommChannelFenceOnThread faild, ret is %d", ret);
    return hixl::FAILED;
  }
  HIXL_LOGI(
      "[HixlBatchPutAndGet] HcommReadOnThread start to read remote flag, param->flag_size=%u, param->local_flag=%lu, "
      "param->remote_flag=%lu",
      param->flag_size, param->local_flag_addr, param->remote_flag_addr);
  ret = HcommReadOnThread(param->thread, param->channel,
                          reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(param->local_flag_addr)),
                          reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(param->remote_flag_addr)), param->flag_size);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED,
              "[HixlBatchPutAndGet] Remote flag read failed. The address information is as follows:dst_buf:%u, "
              "scr_buf:%u, buf_len:%u, ret is %d.",
              param->local_flag_addr, param->remote_flag_addr, param->flag_size, ret);
    return hixl::FAILED;
  }
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeEnd start");
  ret = HcommBatchModeEnd(kBatchTag);
  if (ret != 0) {
    HIXL_LOGE(hixl::FAILED, "[HixlBatchPutAndGet] HcommBatchModeEnd faild, ret is %d", ret);
    return hixl::FAILED;
  }
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeEnd end");
  return hixl::SUCCESS;
}
}  // namespace

namespace hixl {
extern "C" uint32_t HixlBatchPut(HixlOneSideOpParam *param) {
  uint32_t ret = HixlBatchTransfer(true, param);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "[HixlBatchPut] HixlBatchPut faild, ret is %u", ret);
    return FAILED;
  }
  return ret;
}

extern "C" uint32_t HixlBatchGet(HixlOneSideOpParam *param) {
  uint32_t ret = HixlBatchTransfer(false, param);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "[HixlBatchGet] HixlBatchGet faild, ret is %u", ret);
    return FAILED;
  }
  return ret;
}
}

