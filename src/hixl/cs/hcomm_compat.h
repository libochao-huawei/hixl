/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/hcomm_res.h"
#include "common/hixl_log.h"

#ifndef CANN_HIXL_SRC_HCOMM_COMPAT_H_
#define CANN_HIXL_SRC_HCOMM_COMPAT_H_

__attribute__((weak)) HcclResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag, HcommMem mem, void **memHandle) {
  (void)endPointHandle;
  (void)memTag;
  (void)mem;
  (void)memHandle;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommMemUnreg(EndpointHandle endPointHandle, void *memHandle) {
  (void)endPointHandle;
  (void)memHandle;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommMemExport(EndpointHandle endPointHandle, void *memHandle, void **memDesc, uint32_t *memDescLen) {
  (void)endPointHandle;
  (void)memHandle;
  (void)memDesc;
  (void)memDescLen;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommEndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle) {
  (void)endPoint;
  (void)endPointHandle;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommEndpointDestroy(EndpointHandle endPointHandle) {
  (void)endPointHandle;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen, HcommMem *outMem) {
  (void)endpointHandle;
  (void)memDesc;
  (void)descLen;
  (void)outMem;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen) {
  (void)endpointHandle;
  (void)memDesc;
  (void)descLen;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
    uint32_t channelNum, ChannelHandle *channels) {
  (void)endPointHandle;
  (void)engine;
  (void)channelDescs;
  (void)channelNum;
  (void)channels;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum) {
  (void)channels;
  (void)channelNum;
  return HCCL_E_NOT_SUPPORT;
}

__attribute__((weak)) HcclResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList) {
  (void)channelList;
  (void)listNum;
  (void)statusList;
  return HCCL_E_NOT_SUPPORT;
}

#endif  // CANN_HIXL_SRC_HCOMM_COMPAT_H_
