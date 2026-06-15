/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef TESTS_DEPENDS_COMMON_ENV_GUARD_H_
#define TESTS_DEPENDS_COMMON_ENV_GUARD_H_

#include <string>
#include "mmpa/mmpa_api.h"

namespace test {

/// RAII guard that sets an environment variable on construction and unsets it on destruction.
class EnvGuard {
 public:
  EnvGuard(const char *key, const char *value) : key_(key) {
    mmSetEnv(key, value, 1);
  }
  ~EnvGuard() {
    unsetenv(key_.c_str());
  }

 private:
  const std::string key_;
};

}  // namespace test

#endif  // TESTS_DEPENDS_COMMON_ENV_GUARD_H_
