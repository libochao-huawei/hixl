/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_proxy.h"

extern "C" {
__attribute__((weak)) HcclResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag, HcommMem mem,
                                              void **memHandle);
__attribute__((weak)) HcclResult HcommMemUnreg(EndpointHandle endPointHandle, void *memHandle);
__attribute__((weak)) HcclResult HcommMemExport(EndpointHandle endPointHandle, void *memHandle, void **memDesc,
                                                 uint32_t *memDescLen);
__attribute__((weak)) HcclResult HcommEndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle);
__attribute__((weak)) HcclResult HcommEndpointDestroy(EndpointHandle endPointHandle);
__attribute__((weak)) HcclResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen,
                                                 HcommMem *outMem);
__attribute__((weak)) HcclResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen);
__attribute__((weak)) HcclResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine,
                                                    HcommChannelDesc *channelDescs, uint32_t channelNum,
                                                    ChannelHandle *channels);
__attribute__((weak)) HcclResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum);
__attribute__((weak)) HcclResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum,
                                                       int32_t *statusList);
__attribute__((weak)) int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                   const void *src, uint64_t len);
__attribute__((weak)) int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                  const void *src, uint64_t len);
__attribute__((weak)) HcclResult HcommThreadAlloc(CommEngine engine, uint32_t threadNum, uint32_t notifyNumPerThread,
                                                  ThreadHandle *threadHandle);
__attribute__((weak)) HcclResult HcommThreadFree(const ThreadHandle *threads, uint32_t threadNum);
__attribute__((weak)) int32_t HcommBatchModeStart(const char *batchTag);
__attribute__((weak)) int32_t HcommBatchModeEnd(const char *batchTag);
__attribute__((weak)) int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                const void *src, uint64_t len);
__attribute__((weak)) int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
                                                 const void *src, uint64_t len);
__attribute__((weak)) int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel);
}

namespace hixl {

HcclResult HcommProxy::MemReg(EndpointHandle endPointHandle, const char *memTag, HcommMem mem, void **memHandle) {
  if (HcommMemReg == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommMemReg(endPointHandle, memTag, mem, memHandle);
}

HcclResult HcommProxy::MemUnreg(EndpointHandle endPointHandle, void *memHandle) {
  if (HcommMemUnreg == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommMemUnreg(endPointHandle, memHandle);
}

HcclResult HcommProxy::MemExport(EndpointHandle endPointHandle, void *memHandle, void **memDesc, uint32_t *memDescLen) {
  if (HcommMemExport == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommMemExport(endPointHandle, memHandle, memDesc, memDescLen);
}

HcclResult HcommProxy::EndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle) {
  if (HcommEndpointCreate == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommEndpointCreate(endPoint, endPointHandle);
}

HcclResult HcommProxy::EndpointDestroy(EndpointHandle endPointHandle) {
  if (HcommEndpointDestroy == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommEndpointDestroy(endPointHandle);
}

HcclResult HcommProxy::MemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen,
                                  HcommMem *outMem) {
  if (HcommMemImport == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommMemImport(endpointHandle, memDesc, descLen, outMem);
}

HcclResult HcommProxy::MemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen) {
  if (HcommMemUnimport == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommMemUnimport(endpointHandle, memDesc, descLen);
}

HcclResult HcommProxy::ChannelCreate(EndpointHandle endPointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
                                      uint32_t channelNum, ChannelHandle *channels) {
  if (HcommChannelCreate == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommChannelCreate(endPointHandle, engine, channelDescs, channelNum, channels);
}

HcclResult HcommProxy::ChannelDestroy(const ChannelHandle *channels, uint32_t channelNum) {
  if (HcommChannelDestroy == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommChannelDestroy(channels, channelNum);
}

HcclResult HcommProxy::ChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList) {
  if (HcommChannelGetStatus == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommChannelGetStatus(channelList, listNum, statusList);
}

int32_t HcommProxy::WriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                      uint64_t len) {
  if (HcommWriteNbiOnThread == nullptr) {
    return -1;
  }
  return HcommWriteNbiOnThread(thread, channel, dst, src, len);
}

int32_t HcommProxy::ReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                    uint64_t len) {
  if (HcommReadNbiOnThread == nullptr) {
    return -1;
  }
  return HcommReadNbiOnThread(thread, channel, dst, src, len);
}

HcclResult HcommProxy::ThreadAlloc(CommEngine engine, uint32_t threadNum, uint32_t notifyNumPerThread,
                                    ThreadHandle *threadHandle) {
  if (HcommThreadAlloc == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommThreadAlloc(engine, threadNum, notifyNumPerThread, threadHandle);
}

HcclResult HcommProxy::ThreadFree(const ThreadHandle *threads, uint32_t threadNum) {
  if (HcommThreadFree == nullptr) {
    return HCCL_E_NOT_SUPPORT;
  }
  return HcommThreadFree(threads, threadNum);
}

int32_t HcommProxy::BatchModeStart(const char *batchTag) {
  if (HcommBatchModeStart == nullptr) {
    return -1;
  }
  return HcommBatchModeStart(batchTag);
}

int32_t HcommProxy::BatchModeEnd(const char *batchTag) {
  if (HcommBatchModeEnd == nullptr) {
    return -1;
  }
  return HcommBatchModeEnd(batchTag);
}

int32_t HcommProxy::ReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len) {
  if (HcommReadOnThread == nullptr) {
    return -1;
  }
  return HcommReadOnThread(thread, channel, dst, src, len);
}

int32_t HcommProxy::WriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                  uint64_t len) {
  if (HcommWriteOnThread == nullptr) {
    return -1;
  }
  return HcommWriteOnThread(thread, channel, dst, src, len);
}

int32_t HcommProxy::ChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel) {
  if (HcommChannelFenceOnThread == nullptr) {
    return -1;
  }
  return HcommChannelFenceOnThread(thread, channel);
}

}  // namespace hixl
