/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>
#include "gtest/gtest.h"
#include "ascendcl_stub.h"
#include "hccp_proxy.h"

namespace hixl {

namespace {

class ScopedAclStubInstall {
 public:
  explicit ScopedAclStubInstall(llm::AclRuntimeStub *stub) : stub_(stub) { llm::AclRuntimeStub::Install(stub_); }
  ~ScopedAclStubInstall() { llm::AclRuntimeStub::UnInstall(stub_); }

  ScopedAclStubInstall(const ScopedAclStubInstall &) = delete;
  ScopedAclStubInstall &operator=(const ScopedAclStubInstall &) = delete;

 private:
  llm::AclRuntimeStub *stub_;
};

class NegativePhyDevAclStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) override {
    (void)logicDevId;
    *phyDevId = -1;
    return ACL_ERROR_NONE;
  }
};

}  // namespace

TEST(HccpProxyRaGetNotifyAddrLenUt, RejectsNegativeLogicDeviceId) {
  uint64_t notify_addr = 0ULL;
  uint32_t notify_len = 0U;
  EXPECT_EQ(HccpProxy::RaGetNotifyAddrLen(-1, nullptr, notify_addr, notify_len), PARAM_INVALID);
}

TEST(HccpProxyRaGetNotifyAddrLenUt, RejectsNegativePhysicalDeviceId) {
  NegativePhyDevAclStub acl_stub;
  ScopedAclStubInstall guard(&acl_stub);
  uint64_t notify_addr = 0ULL;
  uint32_t notify_len = 0U;
  EXPECT_EQ(HccpProxy::RaGetNotifyAddrLen(0, nullptr, notify_addr, notify_len), PARAM_INVALID);
}

}  // namespace hixl
