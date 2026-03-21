/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HCOMM_PROXY_H_
#define CANN_HIXL_SRC_HIXL_CS_HCOMM_PROXY_H_

#include "hcomm/hcomm_res_defs.h"

namespace hixl {

/**
 * @brief Proxy class that wraps Hcomm (HCCL) weak-symbol interfaces.
 *
 * Each method checks whether the underlying weak symbol is resolved (non-null)
 * before invoking it. If the symbol is not available (e.g. libhcomm.so was not
 * loaded), the method returns a "not supported" error instead of crashing.
 *
 * Usage:
 *   HcclResult ret = HcclProxy::HcommEndpointCreate(&desc, &handle);
 *   if (ret == HCCL_E_NOT_SUPPORT) { ... }
 */
class HcclProxy {
 public:
  HcclProxy() = delete;

  // ---------- Memory registration / unregistration ----------

  static HcclResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag,
                                HcommMem mem, void **memHandle);
  static HcclResult HcommMemUnreg(EndpointHandle endPointHandle, void *memHandle);

  // ---------- Memory export / import ----------

  static HcclResult HcommMemExport(EndpointHandle endPointHandle, void *memHandle,
                                   void **memDesc, uint32_t *memDescLen);
  static HcclResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc,
                                   uint32_t descLen, HcommMem *outMem);
  static HcclResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc,
                                     uint32_t descLen);

  // ---------- Endpoint lifecycle ----------

  static HcclResult HcommEndpointCreate(const EndpointDesc *endPoint,
                                        EndpointHandle *endPointHandle);
  static HcclResult HcommEndpointDestroy(EndpointHandle endPointHandle);

  // ---------- Channel lifecycle ----------

  static HcclResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine,
                                       HcommChannelDesc *channelDescs, uint32_t channelNum,
                                       ChannelHandle *channels);
  static HcclResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum);
  static HcclResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum,
                                          int32_t *statusList);

  // ---------- Thread management ----------

  static HcclResult HcommThreadAlloc(CommEngine engine, uint32_t threadNum,
                                     uint32_t notifyNumPerThread, ThreadHandle *threadHandle);
  static HcclResult HcommThreadFree(const ThreadHandle *threads, uint32_t threadNum);

  // ---------- Batch mode ----------

  static int32_t HcommBatchModeStart(const char *batchTag);
  static int32_t HcommBatchModeEnd(const char *batchTag);

  // ---------- Data transfer (non-blocking) ----------

  static int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel,
                                       void *dst, const void *src, uint64_t len);
  static int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel,
                                      void *dst, const void *src, uint64_t len);

  // ---------- Data transfer (blocking) ----------

  static int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel,
                                   void *dst, const void *src, uint64_t len);
  static int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel,
                                    void *dst, const void *src, uint64_t len);

  // ---------- Synchronization ----------

  static int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel);

  // ---------- Availability check ----------

  /**
   * @brief Check whether a specific Hcomm weak symbol is resolved.
   */
  template <typename FuncPtr>
  static bool IsSymbolAvailable(FuncPtr ptr) {
    return ptr != nullptr;
  }
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HCOMM_PROXY_H_
