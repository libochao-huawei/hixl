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
#include <memory>
#include <string>
#include <unordered_map>

#include "gtest/gtest.h"
#include "ascendcl_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/runtime/src/runtime_stub.h"
#include "runtime_proxy.h"

namespace hixl {
namespace {

constexpr uintptr_t kMockRuntimeHandle = 0x9001ULL;
constexpr uintptr_t kMockAclRtHandle = 0x9002ULL;
constexpr int32_t kRuntimeProxyUtDevice = 3;
constexpr uint64_t kNotifyOffset = 0x1234ULL;

class ScopedMmpaRuntimeMock {
 public:
  explicit ScopedMmpaRuntimeMock(const std::shared_ptr<llm::MmpaStubApiGe> &impl) {
    llm::MmpaStub::GetInstance().SetImpl(impl);
  }
  ~ScopedMmpaRuntimeMock() {
    RuntimeProxy::GetInstance().ResetForTest();
    llm::MmpaStub::GetInstance().Reset();
  }

  ScopedMmpaRuntimeMock(const ScopedMmpaRuntimeMock &) = delete;
  ScopedMmpaRuntimeMock &operator=(const ScopedMmpaRuntimeMock &) = delete;
};

class MockRuntimeStub : public llm::RuntimeStub {
 public:
  rtError_t rtGetDevResAddress(const rtDevResInfo *resInfo, rtDevResAddrInfo *addrInfo) override {
    (void)resInfo;
    if (addrInfo != nullptr && addrInfo->resAddress != nullptr) {
      *static_cast<uint64_t *>(addrInfo->resAddress) = 0xABCDEFULL;
    }
    if (addrInfo != nullptr && addrInfo->len != nullptr) {
      *addrInfo->len = sizeof(uint64_t);
    }
    return RT_ERROR_NONE;
  }

  rtError_t rtNotifyGetAddrOffset(rtNotify_t notify, uint64_t *devAddrOffset) override {
    (void)notify;
    if (devAddrOffset != nullptr) {
      *devAddrOffset = kNotifyOffset;
    }
    return RT_ERROR_NONE;
  }
};

class OkAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetDevice(int32_t *deviceId) override {
    *deviceId = kRuntimeProxyUtDevice;
    return ACL_ERROR_NONE;
  }
};

class RuntimeMmpaOk : public llm::MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    (void)mode;
    if (file_name != nullptr && std::strcmp(file_name, "libruntime.so") == 0) {
      return reinterpret_cast<void *>(kMockRuntimeHandle);
    }
    if (file_name != nullptr && std::strcmp(file_name, "libascendcl.so") == 0) {
      return reinterpret_cast<void *>(kMockAclRtHandle);
    }
    return nullptr;
  }

  void *DlSym(void *handle, const char *func_name) override {
    const uintptr_t handle_value = reinterpret_cast<uintptr_t>(handle);
    if ((handle_value != kMockRuntimeHandle && handle_value != kMockAclRtHandle) || func_name == nullptr) {
      return nullptr;
    }
    return ResolveSymbol(func_name);
  }

  int32_t DlClose(void *handle) override {
    (void)handle;
    ++close_count_;
    return 0;
  }

  int32_t close_count_{0};

 private:
  static void *ResolveSymbol(const char *func_name) {
    static const std::unordered_map<std::string, void *> kSymbols = {
        {"rtGetDevResAddress", reinterpret_cast<void *>(&rtGetDevResAddress)},
        {"rtNotifyGetAddrOffset", reinterpret_cast<void *>(&rtNotifyGetAddrOffset)},
        {"aclrtGetDevice", reinterpret_cast<void *>(&aclrtGetDevice)},
        {"aclrtGetCurrentContext", reinterpret_cast<void *>(&aclrtGetCurrentContext)},
        {"aclrtSetCurrentContext", reinterpret_cast<void *>(&aclrtSetCurrentContext)},
        {"aclrtCreateContext", reinterpret_cast<void *>(&aclrtCreateContext)},
        {"aclrtDestroyContext", reinterpret_cast<void *>(&aclrtDestroyContext)},
        {"aclrtCtxGetCurrentDefaultStream", reinterpret_cast<void *>(&aclrtCtxGetCurrentDefaultStream)},
        {"aclrtMalloc", reinterpret_cast<void *>(&aclrtMalloc)},
        {"aclrtFree", reinterpret_cast<void *>(&aclrtFree)},
        {"aclrtMallocHost", reinterpret_cast<void *>(&aclrtMallocHost)},
        {"aclrtFreeHost", reinterpret_cast<void *>(&aclrtFreeHost)},
        {"aclrtMemcpy", reinterpret_cast<void *>(&aclrtMemcpy)},
        {"aclrtMemcpyAsync", reinterpret_cast<void *>(&aclrtMemcpyAsync)},
        {"aclrtCreateNotify", reinterpret_cast<void *>(&aclrtCreateNotify)},
        {"aclrtDestroyNotify", reinterpret_cast<void *>(&aclrtDestroyNotify)},
        {"aclrtGetNotifyId", reinterpret_cast<void *>(&aclrtGetNotifyId)},
        {"aclrtNotifyBatchReset", reinterpret_cast<void *>(&aclrtNotifyBatchReset)},
        {"aclrtWaitAndResetNotify", reinterpret_cast<void *>(&aclrtWaitAndResetNotify)},
        {"aclrtSynchronizeStreamWithTimeout", reinterpret_cast<void *>(&aclrtSynchronizeStreamWithTimeout)},
        {"aclrtStreamAbort", reinterpret_cast<void *>(&aclrtStreamAbort)},
        {"aclrtHostRegister", reinterpret_cast<void *>(&aclrtHostRegister)},
        {"aclrtHostUnregister", reinterpret_cast<void *>(&aclrtHostUnregister)},
        {"aclrtBinaryLoadFromFile", reinterpret_cast<void *>(&aclrtBinaryLoadFromFile)},
        {"aclrtBinaryGetFunction", reinterpret_cast<void *>(&aclrtBinaryGetFunction)},
        {"aclrtBinaryUnLoad", reinterpret_cast<void *>(&aclrtBinaryUnLoad)},
        {"aclrtKernelArgsInit", reinterpret_cast<void *>(&aclrtKernelArgsInit)},
        {"aclrtKernelArgsAppend", reinterpret_cast<void *>(&aclrtKernelArgsAppend)},
        {"aclrtKernelArgsFinalize", reinterpret_cast<void *>(&aclrtKernelArgsFinalize)},
        {"aclrtLaunchKernelWithConfig", reinterpret_cast<void *>(&aclrtLaunchKernelWithConfig)},
        {"aclrtGetPhyDevIdByLogicDevId", reinterpret_cast<void *>(&aclrtGetPhyDevIdByLogicDevId)},
    };
    const auto it = kSymbols.find(func_name);
    return it == kSymbols.end() ? nullptr : it->second;
  }
};

class RuntimeMmpaDlOpenFail : public RuntimeMmpaOk {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    (void)file_name;
    (void)mode;
    return nullptr;
  }
};

class RuntimeMmpaDlSymFail : public RuntimeMmpaOk {
 public:
  explicit RuntimeMmpaDlSymFail(std::string failed_symbol) : failed_symbol_(std::move(failed_symbol)) {}

  void *DlSym(void *handle, const char *func_name) override {
    if (func_name != nullptr && failed_symbol_ == func_name) {
      return nullptr;
    }
    return RuntimeMmpaOk::DlSym(handle, func_name);
  }

 private:
  std::string failed_symbol_;
};

}  // namespace

TEST(RuntimeProxyUt, DlOpenFailureReturnsAclFailure) {
  auto mmpa = std::make_shared<RuntimeMmpaDlOpenFail>();
  ScopedMmpaRuntimeMock mmpa_guard(mmpa);
  int32_t device_id = -1;
  EXPECT_NE(RuntimeProxy::GetInstance().aclrtGetDevice(&device_id), ACL_ERROR_NONE);
  EXPECT_EQ(device_id, -1);
}

TEST(RuntimeProxyUt, DlSymFailureReturnsRtFailure) {
  auto mmpa = std::make_shared<RuntimeMmpaDlSymFail>("rtNotifyGetAddrOffset");
  ScopedMmpaRuntimeMock mmpa_guard(mmpa);
  uint64_t offset = 0ULL;
  EXPECT_NE(RuntimeProxy::GetInstance().rtNotifyGetAddrOffset(reinterpret_cast<rtNotify_t>(0x1ULL), &offset),
            RT_ERROR_NONE);
  EXPECT_EQ(offset, 0ULL);
}

TEST(RuntimeProxyUt, ResolvedRtSymbolsForwardToRuntimeStub) {
  auto mmpa = std::make_shared<RuntimeMmpaOk>();
  ScopedMmpaRuntimeMock mmpa_guard(mmpa);
  MockRuntimeStub rt_stub;
  llm::RuntimeStub::Install(&rt_stub);

  rtDevResInfo res_info{};
  uint64_t addr = 0ULL;
  uint32_t len = 0U;
  rtDevResAddrInfo addr_info{&addr, &len};
  EXPECT_EQ(RuntimeProxy::GetInstance().rtGetDevResAddress(&res_info, &addr_info), RT_ERROR_NONE);
  EXPECT_EQ(addr, 0xABCDEFULL);
  EXPECT_EQ(len, sizeof(uint64_t));

  uint64_t offset = 0ULL;
  EXPECT_EQ(RuntimeProxy::GetInstance().rtNotifyGetAddrOffset(reinterpret_cast<rtNotify_t>(0x1ULL), &offset),
            RT_ERROR_NONE);
  EXPECT_EQ(offset, kNotifyOffset);

  llm::RuntimeStub::UnInstall(&rt_stub);
}

TEST(RuntimeProxyUt, ResolvedAclrtSymbolsForwardToAclRuntimeStub) {
  auto mmpa = std::make_shared<RuntimeMmpaOk>();
  ScopedMmpaRuntimeMock mmpa_guard(mmpa);
  OkAclRuntimeStub acl_stub;
  llm::AclRuntimeStub::Install(&acl_stub);

  int32_t device_id = -1;
  EXPECT_EQ(RuntimeProxy::GetInstance().aclrtGetDevice(&device_id), ACL_ERROR_NONE);
  EXPECT_EQ(device_id, kRuntimeProxyUtDevice);

  aclrtNotify notify = nullptr;
  EXPECT_EQ(RuntimeProxy::GetInstance().aclrtCreateNotify(&notify, ACL_NOTIFY_DEVICE_USE_ONLY), ACL_ERROR_NONE);
  EXPECT_NE(notify, nullptr);
  EXPECT_EQ(RuntimeProxy::GetInstance().aclrtDestroyNotify(notify), ACL_ERROR_NONE);

  aclrtBinHandle bin_handle = nullptr;
  aclrtFuncHandle func_handle = nullptr;
  EXPECT_EQ(RuntimeProxy::GetInstance().aclrtBinaryLoadFromFile("ut.o", nullptr, &bin_handle), ACL_ERROR_NONE);
  EXPECT_EQ(RuntimeProxy::GetInstance().aclrtBinaryGetFunction(bin_handle, "ut_func", &func_handle), ACL_ERROR_NONE);
  EXPECT_NE(func_handle, nullptr);

  llm::AclRuntimeStub::UnInstall(&acl_stub);
}

}  // namespace hixl
