/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "common/hixl_checker.h"

#include <cstdarg>
#include <cstdio>

namespace hixl {
namespace checker {
namespace {

constexpr size_t kAclLogDetailSize = 384U;

Status AclErrorToHixlStatus(const aclError acl_ret) {
  return (acl_ret == ACL_ERROR_RT_STREAM_SYNC_TIMEOUT) ? TIMEOUT : static_cast<Status>(acl_ret);
}

void LogAclFailure(const Status status, const char *expr_str, const uint32_t acl_ret_u32, const char *fmt,
                   va_list args) {
  HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", expr_str, acl_ret_u32);
  if (fmt == nullptr || fmt[0] == '\0') {
    HIXL_LOGE(status, "Call acl api:%s failed, ret: 0x%X.", expr_str, acl_ret_u32);
    return;
  }

  char detail[kAclLogDetailSize] = {};
  va_list args_copy;
  va_copy(args_copy, args);
  (void)vsnprintf(detail, sizeof(detail), fmt, args_copy);
  va_end(args_copy);
  HIXL_LOGE(status, "Call acl api:%s failed, ret: 0x%X. %s", expr_str, acl_ret_u32, detail);
}

}  // namespace

Status ChkAclRet(const aclError acl_ret, const char *expr_str) {
  const uint32_t acl_ret_u32 = static_cast<uint32_t>(acl_ret);
  const Status status = AclErrorToHixlStatus(acl_ret);
  HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", expr_str, acl_ret_u32);
  HIXL_LOGE(status, "Call acl api:%s failed, ret: 0x%X.", expr_str, acl_ret_u32);
  return status;
}

Status ChkAclRet(const aclError acl_ret, const char *expr_str, const char *fmt, ...) {
  const uint32_t acl_ret_u32 = static_cast<uint32_t>(acl_ret);
  const Status status = AclErrorToHixlStatus(acl_ret);
  va_list args;
  va_start(args, fmt);
  LogAclFailure(status, expr_str, acl_ret_u32, fmt, args);
  va_end(args);
  return status;
}

void ChkAcl(const aclError acl_ret, const char *expr_str) {
  const uint32_t acl_ret_u32 = static_cast<uint32_t>(acl_ret);
  HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", expr_str, acl_ret_u32);
  HIXL_LOGE(FAILED, "Call acl api:%s failed, ret: 0x%X.", expr_str, acl_ret_u32);
}

void ChkAcl(const aclError acl_ret, const char *expr_str, const char *fmt, ...) {
  const uint32_t acl_ret_u32 = static_cast<uint32_t>(acl_ret);
  HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", expr_str, acl_ret_u32);
  if (fmt == nullptr || fmt[0] == '\0') {
    HIXL_LOGE(FAILED, "Call acl api:%s failed, ret: 0x%X.", expr_str, acl_ret_u32);
    return;
  }

  char detail[kAclLogDetailSize] = {};
  va_list args;
  va_start(args, fmt);
  (void)vsnprintf(detail, sizeof(detail), fmt, args);
  va_end(args);
  HIXL_LOGE(FAILED, "Call acl api:%s failed, ret: 0x%X. %s", expr_str, acl_ret_u32, detail);
}

}  // namespace checker
}  // namespace hixl
