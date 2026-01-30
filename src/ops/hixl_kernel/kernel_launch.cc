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
#include "kernel_launch.h"
#include "hixl/hixl.h"

namespace hixl {

extern "C" unsigned int HcclLaunchAicpuKernel(bool is_read, HixlOneSideOpParam *param) {
  if (is_read) {
    // 批量提交读任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      int ret = HcommReadOnThread(param->thread, param->channel, param->dst_buf_list[i],
                                  const_cast<void *>(param->src_buf_list[i]),
                                  param->len_list[i]);  // HcommReadNbi 没有返回值
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
      int ret = HcommWriteOnThread(param->thread, param->channel, param->dst_buf_list[i],
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
  int ret = HcommReadOnThread(param->thread, param->channel, param->local_flag, param->remote_flag,
                              param->flag_size);  // HcommReadNbi 没有返回值
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Memory read failed. The address information is as follows:dst_buf:%p, scr_buf:%p, buf_len:%u.",
              param->local_flag, param->remote_flag, param->flag_size);
    return FAILED;
  }
  return SUCCESS;
}

extern "C" unsigned int HixlBatchPut(HixlOneSideOpParam *param) {
  int ret = HcclLaunchAicpuKernel(true, param);
  return ret;
}

extern "C" unsigned int HixlBatchGet(HixlOneSideOpParam *param) {
  int ret = HcclLaunchAicpuKernel(false, param);
  return ret;
}
}

