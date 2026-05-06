/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_PROXY_RUNTIME_PROXY_H
#define HIXL_PROXY_RUNTIME_PROXY_H

#include <mutex>

#include "acl/acl.h"
#include "runtime/runtime/dev.h"
#include "runtime/runtime/event.h"
#include "runtime/runtime/rts/rts_device.h"

namespace hixl {

class RuntimeProxy {
 public:
  static RuntimeProxy &GetInstance();

  rtError_t rtGetDevResAddress(rtDevResInfo *const res_info, rtDevResAddrInfo *const addr_info);
  rtError_t rtNotifyGetAddrOffset(rtNotify_t notify, uint64_t *dev_addr_offset);

  aclError aclrtGetDevice(int32_t *device_id);
  aclError aclrtGetCurrentContext(aclrtContext *context);
  aclError aclrtSetCurrentContext(aclrtContext context);
  aclError aclrtCreateContext(aclrtContext *context, int32_t device_id);
  aclError aclrtDestroyContext(aclrtContext context);
  aclError aclrtCtxGetCurrentDefaultStream(aclrtStream *stream);
  aclError aclrtMalloc(void **dev_ptr, size_t size, aclrtMemMallocPolicy policy);
  aclError aclrtFree(void *dev_ptr);
  aclError aclrtMallocHost(void **host_ptr, size_t size);
  aclError aclrtFreeHost(void *host_ptr);
  aclError aclrtMemcpy(void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind);
  aclError aclrtMemcpyAsync(void *dst, size_t dest_max, const void *src, size_t src_count, aclrtMemcpyKind kind,
                            aclrtStream stream);
  aclError aclrtCreateNotify(aclrtNotify *notify, uint64_t flag);
  aclError aclrtDestroyNotify(aclrtNotify notify);
  aclError aclrtGetNotifyId(aclrtNotify notify, uint32_t *notify_id);
  aclError aclrtNotifyBatchReset(aclrtNotify *notifies, size_t num);
  aclError aclrtWaitAndResetNotify(aclrtNotify notify, aclrtStream stream, uint32_t timeout);
  aclError aclrtSynchronizeStreamWithTimeout(aclrtStream stream, int32_t timeout);
  aclError aclrtStreamAbort(aclrtStream stream);
  aclError aclrtHostRegister(void *ptr, uint64_t size, aclrtHostRegisterType type, void **dev_ptr);
  aclError aclrtHostUnregister(void *ptr);
  aclError aclrtBinaryLoadFromFile(const char *model_path, aclrtBinaryLoadOptions *options,
                                   aclrtBinHandle *bin_handle);
  aclError aclrtBinaryGetFunction(aclrtBinHandle bin_handle, const char *function_name,
                                  aclrtFuncHandle *func_handle);
  aclError aclrtBinaryUnLoad(aclrtBinHandle bin_handle);
  aclError aclrtKernelArgsInit(aclrtFuncHandle func_handle, aclrtArgsHandle *args_handle);
  aclError aclrtKernelArgsAppend(aclrtArgsHandle args_handle, void *data, size_t size,
                                 aclrtParamHandle *para_handle);
  aclError aclrtKernelArgsFinalize(aclrtArgsHandle args_handle);
  aclError aclrtLaunchKernelWithConfig(aclrtFuncHandle func_handle, uint32_t block_dim, aclrtStream stream,
                                       aclrtLaunchKernelCfg *config, aclrtArgsHandle args_handle, void *reserved);
  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logic_dev_id, int32_t *const phy_dev_id);

  void ResetForTest();

 private:
  RuntimeProxy() = default;
  ~RuntimeProxy();

  RuntimeProxy(const RuntimeProxy &) = delete;
  RuntimeProxy &operator=(const RuntimeProxy &) = delete;

  struct Functions {
    using RtGetDevResAddressFn = rtError_t (*)(rtDevResInfo *const, rtDevResAddrInfo *const);

    RtGetDevResAddressFn rtGetDevResAddress = nullptr;
    decltype(&::rtNotifyGetAddrOffset) rtNotifyGetAddrOffset = nullptr;
    decltype(&::aclrtGetDevice) aclrtGetDevice = nullptr;
    decltype(&::aclrtGetCurrentContext) aclrtGetCurrentContext = nullptr;
    decltype(&::aclrtSetCurrentContext) aclrtSetCurrentContext = nullptr;
    decltype(&::aclrtCreateContext) aclrtCreateContext = nullptr;
    decltype(&::aclrtDestroyContext) aclrtDestroyContext = nullptr;
    decltype(&::aclrtCtxGetCurrentDefaultStream) aclrtCtxGetCurrentDefaultStream = nullptr;
    decltype(&::aclrtMalloc) aclrtMalloc = nullptr;
    decltype(&::aclrtFree) aclrtFree = nullptr;
    decltype(&::aclrtMallocHost) aclrtMallocHost = nullptr;
    decltype(&::aclrtFreeHost) aclrtFreeHost = nullptr;
    decltype(&::aclrtMemcpy) aclrtMemcpy = nullptr;
    decltype(&::aclrtMemcpyAsync) aclrtMemcpyAsync = nullptr;
    decltype(&::aclrtCreateNotify) aclrtCreateNotify = nullptr;
    decltype(&::aclrtDestroyNotify) aclrtDestroyNotify = nullptr;
    decltype(&::aclrtGetNotifyId) aclrtGetNotifyId = nullptr;
    decltype(&::aclrtNotifyBatchReset) aclrtNotifyBatchReset = nullptr;
    decltype(&::aclrtWaitAndResetNotify) aclrtWaitAndResetNotify = nullptr;
    decltype(&::aclrtSynchronizeStreamWithTimeout) aclrtSynchronizeStreamWithTimeout = nullptr;
    decltype(&::aclrtStreamAbort) aclrtStreamAbort = nullptr;
    decltype(&::aclrtHostRegister) aclrtHostRegister = nullptr;
    decltype(&::aclrtHostUnregister) aclrtHostUnregister = nullptr;
    decltype(&::aclrtBinaryLoadFromFile) aclrtBinaryLoadFromFile = nullptr;
    decltype(&::aclrtBinaryGetFunction) aclrtBinaryGetFunction = nullptr;
    decltype(&::aclrtBinaryUnLoad) aclrtBinaryUnLoad = nullptr;
    decltype(&::aclrtKernelArgsInit) aclrtKernelArgsInit = nullptr;
    decltype(&::aclrtKernelArgsAppend) aclrtKernelArgsAppend = nullptr;
    decltype(&::aclrtKernelArgsFinalize) aclrtKernelArgsFinalize = nullptr;
    decltype(&::aclrtLaunchKernelWithConfig) aclrtLaunchKernelWithConfig = nullptr;
    decltype(&::aclrtGetPhyDevIdByLogicDevId) aclrtGetPhyDevIdByLogicDevId = nullptr;
  };

  bool EnsureRtLoaded();
  bool EnsureAclLoaded();
  void ResetLocked();

  void *rt_handle_ = nullptr;
  void *acl_handle_ = nullptr;
  Functions funcs_{};
  std::mutex mu_;
};

}  // namespace hixl

#endif  // HIXL_PROXY_RUNTIME_PROXY_H
