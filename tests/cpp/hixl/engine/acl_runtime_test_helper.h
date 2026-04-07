/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_CPP_HIXL_ENGINE_ACL_RUNTIME_TEST_HELPER_H_
#define CANN_HIXL_TESTS_CPP_HIXL_ENGINE_ACL_RUNTIME_TEST_HELPER_H_

#include "ascendcl_stub.h"

namespace hixl {

class DeviceCountAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  uint32_t device_count_ = 0U;

  aclError aclrtGetDeviceCount(uint32_t *count) override {
    if (count == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *count = device_count_;
    return ACL_SUCCESS;
  }
};

class FailingGetDeviceAclRuntimeStub : public DeviceCountAclRuntimeStub {
 public:
  int get_device_calls_ = 0;

  aclError aclrtGetDevice(int32_t *deviceId) override {
    ++get_device_calls_;
    (void)deviceId;
    return ACL_ERROR_FAILURE;
  }
};

}  // namespace hixl

#endif  // CANN_HIXL_TESTS_CPP_HIXL_ENGINE_ACL_RUNTIME_TEST_HELPER_H_
