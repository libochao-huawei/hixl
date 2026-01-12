/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstring>
#include "hccl_api.h"
#include "securec.h"

#ifdef __cplusplus
extern "C" {
#endif

HcclResult HcommMemReg(void *handle, HcclMem mem, void **mem_handle) {
  static int32_t mem_num_stub = 1;
  (void) handle;
  (void) mem;
  *mem_handle = reinterpret_cast<void *>(mem_num_stub++);
  return HCCL_SUCCESS;
}

HcclResult HcommMemUnreg(void *handle, void *mem_handle) {
  (void)handle;
  (void)mem_handle;
  return HCCL_SUCCESS;
}

HcclResult HcommMemExport(void *handle, const void *mem_handle, void **mem_desc, uint32_t *desc_len) {
  (void)handle;
  (void)mem_handle;
  static std::string desc = "test_desc2";
  *mem_desc = const_cast<char *>(desc.c_str());
  *desc_len = desc.size();
  return HCCL_SUCCESS;
}

HcclResult HcommEndPointCreate(const EndPointInfo *endpoint, void **handle) {
  (void)endpoint;

  static int32_t ep_num_stub = 1;
  *handle = reinterpret_cast<void *>(ep_num_stub++);
  return HCCL_SUCCESS;
}

HcclResult HcommEndPointDestroy(void *handle) {
  (void)handle;
  return HCCL_SUCCESS;
}

HcclResult HcommMemImport(void *end_point_handle, const void *mem_desc, uint32_t desc_len, HcommBuf *out_buf) {
  (void)end_point_handle;
  (void)desc_len;

  if (mem_desc == nullptr || out_buf == nullptr || desc_len == 0) {
    return HCCL_E_INTERNAL;
  }

  if (desc_len == 4 && std::memcmp(mem_desc, "FAIL", 4) == 0) {
    return HCCL_E_INTERNAL;
  }

  out_buf->addr = const_cast<void *>(mem_desc);
  out_buf->len = desc_len;
  return HCCL_SUCCESS;
}

HcclResult HcommMemClose(void *endPointHandle, const HcommBuf *buf) {
  (void)endPointHandle;
  (void)buf;
  return HCCL_SUCCESS;
}

HcclResult HcommChannelCreate(void **end_point_handle, CommEngine engine, HcommChannelDescNew *channel_desc_list,
                              uint32_t list_num, const void **mem_handle_list, uint32_t mem_handle_list_num,
                              ChannelHandle *channel_list) {
  (void)end_point_handle;
  (void)engine;
  (void)channel_desc_list;
  (void)list_num;
  (void)mem_handle_list;
  (void)mem_handle_list_num;
  static int32_t chn_num_stub = 1;
  *channel_list = static_cast<ChannelHandle>(chn_num_stub++);
  return HCCL_SUCCESS;
}

HcclResult HcommChannelDestroy(const ChannelHandle *channel_list, uint32_t list_num) {
  (void)channel_list;
  (void)list_num;
  return HCCL_SUCCESS;
}

HcclResult HcommChannelGetStatus(const ChannelHandle *channel_list, uint32_t list_num, int32_t *status_list) {
  (void)channel_list;
  (void)list_num;
  (void)status_list;
  return HCCL_SUCCESS;
}

void HcommChannelFence(ChannelHandle channel) {
  (void)channel;
}

void HcommWriteNbi(ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)channel;
  if (len == 0) {
    return;
  }
  if (dst == nullptr || src == nullptr) {
    return;
  }
  memcpy_s(dst, len, src, len);
}

void HcommReadNbi(ChannelHandle channel, void *dst, void *src, uint64_t len) {
  (void)channel;
  if (len == 0) {
    return;
  }
  if (dst == nullptr || src == nullptr) {
    return;
  }
  memcpy_s(dst, len, src, len);
}

#ifdef __cplusplus
}
#endif
