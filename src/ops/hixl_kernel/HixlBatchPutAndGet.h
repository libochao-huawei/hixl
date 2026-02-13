/**
* Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "hccl/hcomm_primitives.h"
#ifndef CANN_HIXL_SRC_HIXL_OPS_HIXL_KERNEL_KERNEL_LAUNCH_H
#define CANN_HIXL_SRC_HIXL_OPS_HIXL_KERNEL_KERNEL_LAUNCH_H

/**
* @brief 批量读取server侧的内存内容
* @param [in] thread 线程句柄
* @param [in] channel 通道句柄
* @param [in] list_num 本次传输任务的数目
* @param [in] dst_buf_list 记录了本次传输任务中每组的目标侧内存地址
* @param [in] src_buf_list 记录了本次传输任务中每组的源侧内存地址
* @param [in] len_list 记录了本次传输任务中每组任务的内存偏移量大小
* @param [in] remote_flag 记录了本次传输任务中remote_flag的内存地址
* @param [in] local_flag 记录了本次传输任务中local_flag的内存地址
* @param [in] flag_size 记录了本次传输任务中flag的内存大小
* @return 成功:SUCCESS, 失败:其它.
  */
struct HixlOneSideOpParam {
  ThreadHandle thread;
  ChannelHandle channel;
  uint32_t list_num;
  void **dst_buf_list;
  const void **src_buf_list;
  uint64_t *len_list;
  void *remote_flag;
  void *local_flag;
  uint32_t flag_size;
};
extern "C" unsigned int HcclLaunchAicpuKernel(bool is_read, HixlOneSideOpParam *param);

extern "C" unsigned int HixlBatchPut(HixlOneSideOpParam *param);

extern "C" unsigned int HixlBatchGet(HixlOneSideOpParam *param);
#endif
