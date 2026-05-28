/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_OPTIONAL_ACLRT_CONTEXT_H_
#define CANN_HIXL_SRC_HIXL_COMMON_OPTIONAL_ACLRT_CONTEXT_H_

#include "acl/acl.h"
#include "hixl/hixl_types.h"

namespace hixl {
class OptionalAclrtContext {
 public:
  Status GetCurrentContext();
  Status SetCurrentContext() const;

 private:
  bool enabled_ = false;
  aclrtContext ctx_ = nullptr;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_OPTIONAL_ACLRT_CONTEXT_H_
