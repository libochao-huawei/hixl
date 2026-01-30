/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "load_kernel.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <limits.h>
#include <unistd.h>

#include "runtime/runtime/rt.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace hixl {
namespace {

constexpr uint32_t kCpuKernelMode = 0U;

static Status SwitchDevice(int32_t target_device, int32_t &old_device, bool &need_restore) {
  old_device = -1;
  need_restore = false;

  rtError_t rret = rtGetDevice(&old_device);
  if (rret != RT_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[LoadKernel] rtGetDevice failed. ret=%d", static_cast<int32_t>(rret));
    return FAILED;
  }

  if (old_device == target_device) {
    return SUCCESS;
  }

  rret = rtSetDevice(target_device);
  if (rret != RT_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[LoadKernel] rtSetDevice failed. target=%d ret=%d",
              target_device, static_cast<int32_t>(rret));
    return FAILED;
  }

  need_restore = true;
  return SUCCESS;
}

static Status CanonicalizePath(const char *path, char *out_real, uint32_t out_real_size) {
  if (path == nullptr) {
    return PARAM_INVALID;
  }
  if (out_real == nullptr) {
    return PARAM_INVALID;
  }
  if (out_real_size == 0U) {
    return PARAM_INVALID;
  }

  errno = 0;

  char *p = realpath(path, out_real);
  if (p == nullptr) {
    HIXL_LOGE(FAILED, "[LoadKernel] realpath failed. path=%s errno=%d", path, errno);
    return FAILED;
  }

  return SUCCESS;
}

static Status LoadBinaryFromJson(const char *json_path, aclrtBinHandle &bin_handle) {
  if (json_path == nullptr) {
    return PARAM_INVALID;
  }

  char real_path[PATH_MAX] = {0};

  Status pret = CanonicalizePath(json_path, real_path, static_cast<uint32_t>(sizeof(real_path)));
  if (pret != SUCCESS) {
    return pret;
  }

  aclrtBinaryLoadOptions load_options{};
  aclrtBinaryLoadOption option{};
  option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
  option.value.cpuKernelMode = kCpuKernelMode;
  load_options.numOpt = 1U;
  load_options.options = &option;

  aclError aerr = aclrtBinaryLoadFromFile(real_path, &load_options, &bin_handle);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[LoadKernel] aclrtBinaryLoadFromFile failed. path=%s ret=%d",
              real_path, static_cast<int32_t>(aerr));
    return FAILED;
  }

  HIXL_LOGI("[LoadKernel] aclrtBinaryLoadFromFile success. path=%s handle=%p", real_path, bin_handle);
  return SUCCESS;
}

static Status GetFuncStub(aclrtBinHandle bin_handle, const char *func_name, aclrtFuncHandle &func_handle) {


  if (bin_handle == nullptr) {
    return PARAM_INVALID;
  }
  if (func_name == nullptr) {
    return PARAM_INVALID;
  }


  aclError aerr = aclrtBinaryGetFunction(bin_handle, func_name, &func_handle);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[LoadKernel] aclrtBinaryGetFunction failed. func=%s ret=%d",
              func_name, static_cast<int32_t>(aerr));
    return FAILED;
  }


  HIXL_LOGI("[LoadKernel] resolve stub success. func=%s stub=%p", func_name, func_handle);
  return SUCCESS;
}

}  // namespace

Status LoadUbKernelAndResolveStubs(int32_t device_id,
                                  const char *json_path,
                                  const char *func_get,
                                  const char *func_put,
                                  aclrtBinHandle &bin_handle,
                                  UbKernelStubs &stubs) {
  stubs.batchGet = nullptr;
  stubs.batchPut = nullptr;

  int32_t old_device = -1;
  bool need_restore = false;

  Status sret = SwitchDevice(device_id, old_device, need_restore);
  if (sret != SUCCESS) {
    return sret;
  }

  HIXL_DISMISSABLE_GUARD(dev_restore, [&]() {
    if (need_restore) {
      (void)rtSetDevice(old_device);
    }
  });

  if (bin_handle == nullptr) {
    Status lret = LoadBinaryFromJson(json_path, bin_handle);
    if (lret != SUCCESS) {
      return lret;
    }
  }

  Status fret = GetFuncStub(bin_handle, func_get, stubs.batchGet);
  if (fret != SUCCESS) {
    return fret;
  }

  fret = GetFuncStub(bin_handle, func_put, stubs.batchPut);
  if (fret != SUCCESS) {
    return fret;
  }

  return SUCCESS;
}

}  // namespace hixl
