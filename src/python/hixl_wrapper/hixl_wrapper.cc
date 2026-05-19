/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_wrapper.h"
#include "hixl/hixl.h"
#include "hixl/hixl_types.h"
#include <map>

extern "C" {

HixlHandle HixlCreate() {
  return new hixl::Hixl();
}

void HixlDestroy(HixlHandle handle) {
  if (handle) {
    delete static_cast<hixl::Hixl *>(handle);
  }
}

uint32_t HixlInitialize(HixlHandle handle, const char *local_engine, const char **option_keys, const char **option_vals,
                        int option_count) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl) return hixl::PARAM_INVALID;

  std::map<hixl::AscendString, hixl::AscendString> options;
  for (int i = 0; i < option_count; ++i) {
    options[hixl::AscendString(option_keys[i])] = hixl::AscendString(option_vals[i]);
  }

  return hixl->Initialize(hixl::AscendString(local_engine), options);
}

}  // extern "C"
