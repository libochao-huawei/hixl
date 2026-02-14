/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <string>
#include <memory>
#include "common/hixl_log.h"
#include "hixl_batch_transfer.h"
#include "hixl/hixl.h"

namespace hixl {

extern "C" unsigned int HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  HIXL_LOGI("[HixlBatchPutAndGet] HixlBatchTransfer start");
  if (param == nullptr) {
    HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] param is nullptr");
    return FAILED;
  }
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeStart start");
  const char *batchTag = "HixlKernel";
  int32_t ret = HcommBatchModeStart(batchTag);
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeStart end");
  if (ret != 0) {
    HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] HcommBatchModeStart faild");
    return FAILED;
  }
  if (is_read) {
    // 批量提交读任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start1, list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%p",
          param->list_num, i, param->thread, param->channel, i, param->dst_buf_list[i], i, param->src_buf_list[i], i,
          param->len_list[i]);
      ret = HcommReadOnThread(param->thread, param->channel, param->dst_buf_list[i],
                              const_cast<void *>(param->src_buf_list[i]), param->len_list[i]);
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "Memory read failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u.",
                  param->dst_buf_list[i], param->src_buf_list[i], param->len_list[i]);
        return FAILED;
      }
    }
  } else {
    // 批量提交写任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI(
          "[HixlBatchPutAndGet] HcommReadOnThread start1 | list_num=%u, i=%u, thread=%u, channel=%u, "
          "dst_buf_list[%u]=%p, src_buf_list[%u]=%p, len_list[%u]=%p",
          param->list_num, i, param->thread, param->channel, i, param->dst_buf_list[i], i, param->src_buf_list[i], i,
          param->len_list[i]);
      ret = HcommWriteOnThread(param->thread, param->channel, param->dst_buf_list[i],
                               const_cast<void *>(param->src_buf_list[i]),
                               param->len_list[i]);  // HcommWriteNbi 没有返回值
      if (ret != 0) {
        HIXL_LOGE(FAILED,
                  "Memory read failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u.",
                  param->dst_buf_list[i], param->src_buf_list[i], param->len_list[i]);
        return FAILED;
      }
    }
  }
  ret = HcommChannelFenceOnThread(param->thread, param->channel); //接口尚未提供
  HIXL_LOGI("[HixlBatchPutAndGet] HcommReadOnThread start2");
  HIXL_LOGI("[HixlBatchPutAndGet] param->flag_size=%u, param->local_flag=%lu, param->remote_flag=%lu", param->flag_size,
            param->local_flag, param->remote_flag);
  ret = HcommReadOnThread(param->thread, param->channel,
                          reinterpret_cast<void *>(static_cast<uintptr_t>(param->local_flag)),
                          reinterpret_cast<void *>(static_cast<uintptr_t>(param->remote_flag)), param->flag_size);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Memory read failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u.",
              param->local_flag, param->remote_flag, param->flag_size);
    return FAILED;
  }
  ret = HcommBatchModeEnd(batchTag);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "[HixlBatchPutAndGet] HcommBatchModeEnd faild");
    return FAILED;
  }
  HIXL_LOGI("[HixlBatchPutAndGet] HcommBatchModeEnd end");
  return SUCCESS;
}

extern "C" unsigned int HixlBatchPut(HixlOneSideOpParam *param) {
  unsigned int ret = HixlBatchTransfer(true, param);
  return ret;
}

extern "C" unsigned int HixlBatchGet(HixlOneSideOpParam *param) {
  unsigned int ret = HixlBatchTransfer(false, param);
  return ret;
}
}

