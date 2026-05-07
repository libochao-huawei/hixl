/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "runtime_proxy.h"

#include <cstdint>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "mmpa/mmpa_api.h"

namespace hixl {
namespace {
constexpr const char *kRuntimeSo = "libruntime.so";
constexpr const char *kAclRtSo = "libascendcl.so";

rtError_t RuntimeProxyRtFailure() {
  return static_cast<rtError_t>(ACL_ERROR_RT_INTERNAL_ERROR);
}

aclError RuntimeProxyAclFailure() {
  return ACL_ERROR_RT_INTERNAL_ERROR;
}
}  // namespace

RuntimeProxy &RuntimeProxy::GetInstance() {
  static RuntimeProxy inst;
  return inst;
}

RuntimeProxy::~RuntimeProxy() {
  std::lock_guard<std::mutex> lock(mu_);
  ResetLocked();
}

void RuntimeProxy::ResetForTest() {
  std::lock_guard<std::mutex> lock(mu_);
  ResetLocked();
}

void RuntimeProxy::ResetLocked() {
  if (rt_handle_ != nullptr) {
    HIXL_LOGI("[RuntimeProxy] mmDlclose %s", kRuntimeSo);
    (void)mmDlclose(rt_handle_);
    rt_handle_ = nullptr;
  }
  if (acl_handle_ != nullptr) {
    HIXL_LOGI("[RuntimeProxy] mmDlclose %s", kAclRtSo);
    (void)mmDlclose(acl_handle_);
    acl_handle_ = nullptr;
  }
  funcs_ = Functions{};
}

bool RuntimeProxy::EnsureRtLoaded() {
  std::lock_guard<std::mutex> lock(mu_);
  if (rt_handle_ != nullptr) {
    return true;
  }
  const int32_t dl_mode = static_cast<int32_t>(static_cast<uint32_t>(MMPA_RTLD_NOW) |
                                               static_cast<uint32_t>(MMPA_RTLD_GLOBAL));
  void *handle = mmDlopen(kRuntimeSo, dl_mode);
  if (handle == nullptr) {
    HIXL_LOGE(FAILED, "[RuntimeProxy] mmDlopen %s failed: %s", kRuntimeSo, mmDlerror());
    return false;
  }
  rt_handle_ = handle;

#define HIXL_RUNTIME_LOAD_SYMBOL(name)                                                                          \
  do {                                                                                                          \
    funcs_.name = reinterpret_cast<decltype(funcs_.name)>(mmDlsym(rt_handle_, #name));                          \
    if (funcs_.name == nullptr) {                                                                               \
      HIXL_LOGE(FAILED, "[RuntimeProxy] mmDlsym %s from %s failed: %s", #name, kRuntimeSo, mmDlerror());        \
      ResetLocked();                                                                                            \
      return false;                                                                                             \
    }                                                                                                           \
  } while (0)

  HIXL_RUNTIME_LOAD_SYMBOL(rtGetDevResAddress);
  HIXL_RUNTIME_LOAD_SYMBOL(rtNotifyGetAddrOffset);

#undef HIXL_RUNTIME_LOAD_SYMBOL

  return true;
}

bool RuntimeProxy::EnsureAclLoaded() {
  std::lock_guard<std::mutex> lock(mu_);
  if (acl_handle_ != nullptr) {
    return true;
  }
  const int32_t dl_mode = static_cast<int32_t>(static_cast<uint32_t>(MMPA_RTLD_NOW) |
                                               static_cast<uint32_t>(MMPA_RTLD_GLOBAL));
  void *handle = mmDlopen(kAclRtSo, dl_mode);
  if (handle == nullptr) {
    HIXL_LOGE(FAILED, "[RuntimeProxy] mmDlopen %s failed: %s", kAclRtSo, mmDlerror());
    return false;
  }
  acl_handle_ = handle;

#define HIXL_RUNTIME_LOAD_SYMBOL(name)                                                                          \
  do {                                                                                                          \
    funcs_.name = reinterpret_cast<decltype(funcs_.name)>(mmDlsym(acl_handle_, #name));                         \
    if (funcs_.name == nullptr) {                                                                               \
      HIXL_LOGE(FAILED, "[RuntimeProxy] mmDlsym %s from %s failed: %s", #name, kAclRtSo, mmDlerror());          \
      ResetLocked();                                                                                            \
      return false;                                                                                             \
    }                                                                                                           \
  } while (0)

  HIXL_RUNTIME_LOAD_SYMBOL(aclrtGetDevice);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtGetCurrentContext);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtSetCurrentContext);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtCreateContext);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtDestroyContext);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtCtxGetCurrentDefaultStream);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtMalloc);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtFree);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtMallocHost);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtFreeHost);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtMemcpy);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtMemcpyAsync);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtCreateNotify);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtDestroyNotify);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtGetNotifyId);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtNotifyBatchReset);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtWaitAndResetNotify);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtSynchronizeStreamWithTimeout);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtStreamAbort);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtHostRegister);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtHostUnregister);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtBinaryLoadFromFile);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtBinaryGetFunction);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtBinaryUnLoad);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtKernelArgsInit);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtKernelArgsAppend);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtKernelArgsFinalize);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtLaunchKernelWithConfig);
  HIXL_RUNTIME_LOAD_SYMBOL(aclrtGetPhyDevIdByLogicDevId);

#undef HIXL_RUNTIME_LOAD_SYMBOL

  return true;
}

rtError_t RuntimeProxy::rtGetDevResAddress(rtDevResInfo *const res_info, rtDevResAddrInfo *const addr_info) {
  if (!EnsureRtLoaded()) {
    return RuntimeProxyRtFailure();
  }
  return funcs_.rtGetDevResAddress(res_info, addr_info);
}

rtError_t RuntimeProxy::rtNotifyGetAddrOffset(rtNotify_t notify, uint64_t *dev_addr_offset) {
  if (!EnsureRtLoaded()) {
    return RuntimeProxyRtFailure();
  }
  return funcs_.rtNotifyGetAddrOffset(notify, dev_addr_offset);
}

#define HIXL_RUNTIME_PROXY_ACL_METHOD(ret_type, name, args, call_args) \
  ret_type RuntimeProxy::name args {                                   \
    if (!EnsureAclLoaded()) {                                          \
      return RuntimeProxyAclFailure();                                 \
    }                                                                  \
    return funcs_.name call_args;                                      \
  }

HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtGetDevice, (int32_t *device_id), (device_id))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtGetCurrentContext, (aclrtContext *context), (context))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtSetCurrentContext, (aclrtContext context), (context))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtCreateContext, (aclrtContext *context, int32_t device_id),
                              (context, device_id))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtDestroyContext, (aclrtContext context), (context))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtCtxGetCurrentDefaultStream, (aclrtStream *stream), (stream))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtMalloc, (void **dev_ptr, size_t size, aclrtMemMallocPolicy policy),
                              (dev_ptr, size, policy))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtFree, (void *dev_ptr), (dev_ptr))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtMallocHost, (void **host_ptr, size_t size), (host_ptr, size))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtFreeHost, (void *host_ptr), (host_ptr))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtMemcpy,
                              (void *dst, size_t dest_max, const void *src, size_t count, aclrtMemcpyKind kind),
                              (dst, dest_max, src, count, kind))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtMemcpyAsync,
                              (void *dst, size_t dest_max, const void *src, size_t src_count, aclrtMemcpyKind kind,
                               aclrtStream stream),
                              (dst, dest_max, src, src_count, kind, stream))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtCreateNotify, (aclrtNotify *notify, uint64_t flag), (notify, flag))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtDestroyNotify, (aclrtNotify notify), (notify))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtGetNotifyId, (aclrtNotify notify, uint32_t *notify_id),
                              (notify, notify_id))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtNotifyBatchReset, (aclrtNotify *notifies, size_t num), (notifies, num))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtWaitAndResetNotify,
                              (aclrtNotify notify, aclrtStream stream, uint32_t timeout), (notify, stream, timeout))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtSynchronizeStreamWithTimeout, (aclrtStream stream, int32_t timeout),
                              (stream, timeout))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtStreamAbort, (aclrtStream stream), (stream))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtHostRegister,
                              (void *ptr, uint64_t size, aclrtHostRegisterType type, void **dev_ptr),
                              (ptr, size, type, dev_ptr))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtHostUnregister, (void *ptr), (ptr))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtBinaryLoadFromFile,
                              (const char *model_path, aclrtBinaryLoadOptions *options, aclrtBinHandle *bin_handle),
                              (model_path, options, bin_handle))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtBinaryGetFunction,
                              (aclrtBinHandle bin_handle, const char *function_name, aclrtFuncHandle *func_handle),
                              (bin_handle, function_name, func_handle))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtBinaryUnLoad, (aclrtBinHandle bin_handle), (bin_handle))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtKernelArgsInit,
                              (aclrtFuncHandle func_handle, aclrtArgsHandle *args_handle),
                              (func_handle, args_handle))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtKernelArgsAppend,
                              (aclrtArgsHandle args_handle, void *data, size_t size, aclrtParamHandle *para_handle),
                              (args_handle, data, size, para_handle))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtKernelArgsFinalize, (aclrtArgsHandle args_handle), (args_handle))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtLaunchKernelWithConfig,
                              (aclrtFuncHandle func_handle, uint32_t block_dim, aclrtStream stream,
                               aclrtLaunchKernelCfg *config, aclrtArgsHandle args_handle, void *reserved),
                              (func_handle, block_dim, stream, config, args_handle, reserved))
HIXL_RUNTIME_PROXY_ACL_METHOD(aclError, aclrtGetPhyDevIdByLogicDevId,
                              (const int32_t logic_dev_id, int32_t *const phy_dev_id),
                              (logic_dev_id, phy_dev_id))

#undef HIXL_RUNTIME_PROXY_ACL_METHOD

}  // namespace hixl
