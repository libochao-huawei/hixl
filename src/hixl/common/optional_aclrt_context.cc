/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/optional_aclrt_context.h"

#include "common/hixl_checker.h"

namespace hixl {
Status OptionalAclrtContext::GetCurrentContext() {
  uint32_t device_count = 0U;
  HIXL_CHK_ACL_RET(aclrtGetDeviceCount(&device_count), "aclrtGetDeviceCount failed");
  enabled_ = (device_count > 0U);
  ctx_ = nullptr;
  if (!enabled_) {
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&ctx_));
  HIXL_LOGI("aclrtGetCurrentContext ctx=%p", ctx_);
  return SUCCESS;
}

Status OptionalAclrtContext::SetCurrentContext() const {
  if (!enabled_ || ctx_ == nullptr) {
    return SUCCESS;
  }
  HIXL_CHK_ACL_RET(aclrtSetCurrentContext(ctx_));
  HIXL_LOGI("aclrtSetCurrentContext ctx=%p", ctx_);
  return SUCCESS;
}
}  // namespace hixl
