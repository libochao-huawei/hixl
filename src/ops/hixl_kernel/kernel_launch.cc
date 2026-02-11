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
  HIXL_LOGI("[JZY] HcclLaunchAicpuKernel start");
  if (param == nullptr) {
      HIXL_LOGE(FAILED, "[JZY] param is nullptr");
      return FAILED;
  }
  HIXL_LOGI("[JZY][HcclLaunchAicpuKernel] HixlOneSideOpParam: thread=%p, channel=%p, list_num=%u",
          param->thread, param->channel, param->list_num);
  // 打印标志信息
  HIXL_LOGI("[JZY][HcclLaunchAicpuKernel]   remote_flag=%p, local_flag=%p, flag_size=%u",
          param->remote_flag, param->local_flag, param->flag_size);
  if (is_read) {
    // 批量提交读任务
    for (uint32_t i = 0; i < param->list_num; i++) {
      HIXL_LOGI("[JZY] HcommWriteOnThread start");
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
      HIXL_LOGI("[JZY] HcommWriteOnThread start");
      HIXL_LOGI("[JZY] param->thread=%u", param->thread);
      HIXL_LOGI("[JZY] param->channel=%u", param->channel);
      HIXL_LOGI("[JZY] param->dst_buf_list=%p", param->dst_buf_list[i]);
      HIXL_LOGI("[JZY] param->src_buf_list=%p", param->src_buf_list[i]);
      HIXL_LOGI("[JZY] param->len_list=%p", param->len_list[i]);
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
  HIXL_LOGI("[JZY] param->flag_size=%u", param->flag_size);
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
  int ret = HcclLaunchAicpuKernel(false, param);
  return ret;
}

extern "C" unsigned int HixlBatchGet(HixlOneSideOpParam *param) {
  int ret = HcclLaunchAicpuKernel(true, param);
  return ret;
}
}