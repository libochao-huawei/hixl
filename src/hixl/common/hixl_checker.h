/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_HIXL_CHECKER_H_
#define CANN_HIXL_SRC_HIXL_COMMON_HIXL_CHECKER_H_

#include "hixl/hixl_types.h"
#include "base/err_msg.h"
#include "acl/acl.h"
#include "hixl_log.h"

#ifdef PRODUCT_SIDE_DEVICE
#define HIXL_REPORT_ERR_MSG(...)
#else
#define HIXL_REPORT_ERR_MSG(...) REPORT_INNER_ERR_MSG(__VA_ARGS__)
#endif

// If expr is not SUCCESS, print the log and return the same value
#define HIXL_CHK_STATUS_RET(expr, ...)                                    \
  do {                                                                    \
    const hixl::Status _chk_status = (expr);                              \
    if (_chk_status != hixl::SUCCESS) {                                   \
      HIXL_REPORT_ERR_MSG("E19999", "Call " #expr " fail. " __VA_ARGS__); \
      HIXL_LOGE((_chk_status), __VA_ARGS__);                              \
      return _chk_status;                                                 \
    }                                                                     \
  } while (false)

// If expr is not SUCCESS, print the log and do not execute return
#define HIXL_CHK_STATUS(expr, ...)                                        \
  do {                                                                    \
    const hixl::Status _chk_status = (expr);                              \
    if (_chk_status != hixl::SUCCESS) {                                   \
      HIXL_REPORT_ERR_MSG("E19999", "Call " #expr " fail. " __VA_ARGS__); \
      HIXL_LOGE(_chk_status, __VA_ARGS__);                                \
    }                                                                     \
  } while (false)

// If expr is not true, print the log and return the specified status
#define HIXL_CHK_BOOL_RET_STATUS(expr, _status, ...) \
  do {                                               \
    const bool b = (expr);                           \
    if (!b) {                                        \
      HIXL_REPORT_ERR_MSG("E19999", __VA_ARGS__);    \
      HIXL_LOGE((_status), __VA_ARGS__);             \
      return (_status);                              \
    }                                                \
  } while (false)

// If expr is true, print info log and return the specified status
#define HIXL_CHK_BOOL_RET_SPECIAL_STATUS(expr, _status, ...) \
  do {                                                       \
    const bool b = (expr);                                   \
    if (b) {                                                 \
      HIXL_LOGI(__VA_ARGS__);                                \
      return (_status);                                      \
    }                                                        \
  } while (false)

// Check if the parameter is null. If yes, return PARAM_INVALID and record the error
#define HIXL_CHECK_NOTNULL(val, ...)                                                         \
  do {                                                                                       \
    if ((val) == nullptr) {                                                                  \
      HIXL_REPORT_ERR_MSG("E19999", "Param:" #val " is nullptr, check invalid" __VA_ARGS__); \
      HIXL_LOGE(hixl::PARAM_INVALID, "[Check][Param:" #val "]null is invalid" __VA_ARGS__);  \
      return hixl::PARAM_INVALID;                                                            \
    }                                                                                        \
  } while (false)

// If expr is not 0, print the log and return
#define HIXL_CHK_HCCL_RET(expr)                                                                       \
  do {                                                                                                \
    const HcclResult _ret = (expr);                                                                   \
    if (_ret != HCCL_SUCCESS) {                                                                       \
      HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));   \
      const auto _hixl_ret = hixl::HcclError2Status(_ret);                                            \
      HIXL_LOGE(_hixl_ret, "Call hccl api:%s failed, ret: 0x%X", #expr, static_cast<uint32_t>(_ret)); \
      return _hixl_ret;                                                                               \
    }                                                                                                 \
  } while (false)

#define HIXL_CHK_HCCL(expr)                                                                           \
  do {                                                                                                \
    const HcclResult _ret = (expr);                                                                   \
    if (_ret != HCCL_SUCCESS) {                                                                       \
      HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));   \
      const auto _hixl_ret = hixl::HcclError2Status(_ret);                                            \
      HIXL_LOGE(_hixl_ret, "Call hccl api:%s failed, ret: 0x%X", #expr, static_cast<uint32_t>(_ret)); \
    }                                                                                                 \
  } while (false)

#define HIXL_CHK_ACL_RET_BODY(expr, fmt, ...)                                                                        \
  do {                                                                                                               \
    const aclError _acl_ret = (expr);                                                                                \
    if (_acl_ret != ACL_SUCCESS) {                                                                                   \
      HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_acl_ret));              \
      const hixl::Status _acl_hixl_status =                                                                          \
          (_acl_ret == ACL_ERROR_RT_STREAM_SYNC_TIMEOUT) ? hixl::TIMEOUT : static_cast<hixl::Status>(_acl_ret);      \
      HIXL_LOGE(_acl_hixl_status, "Call acl api:%s failed, ret: 0x%X. " fmt, #expr, static_cast<uint32_t>(_acl_ret), \
                ##__VA_ARGS__);                                                                                      \
      return _acl_hixl_status;                                                                                       \
    }                                                                                                                \
  } while (false)

#define HIXL_CHK_ACL_RET1(expr) HIXL_CHK_ACL_RET_BODY(expr, "")
#define HIXL_CHK_ACL_RET2(expr, fmt) HIXL_CHK_ACL_RET_BODY(expr, fmt)
#define HIXL_CHK_ACL_RET3(expr, fmt, a) HIXL_CHK_ACL_RET_BODY(expr, fmt, a)
#define HIXL_CHK_ACL_RET4(expr, fmt, a, b) HIXL_CHK_ACL_RET_BODY(expr, fmt, a, b)
#define HIXL_CHK_ACL_RET5(expr, fmt, a, b, c) HIXL_CHK_ACL_RET_BODY(expr, fmt, a, b, c)
#define HIXL_CHK_ACL_RET6(expr, fmt, a, b, c, d) HIXL_CHK_ACL_RET_BODY(expr, fmt, a, b, c, d)
#define HIXL_CHK_ACL_RET7(expr, fmt, a, b, c, d, e) HIXL_CHK_ACL_RET_BODY(expr, fmt, a, b, c, d, e)
#define HIXL_CHK_ACL_RET8(expr, fmt, a, b, c, d, e, f) HIXL_CHK_ACL_RET_BODY(expr, fmt, a, b, c, d, e, f)
#define HIXL_CHK_ACL_RET_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
#define HIXL_CHK_ACL_RET(...)                                                                                         \
  HIXL_CHK_ACL_RET_GET_MACRO(__VA_ARGS__, HIXL_CHK_ACL_RET8, HIXL_CHK_ACL_RET7, HIXL_CHK_ACL_RET6, HIXL_CHK_ACL_RET5, \
                             HIXL_CHK_ACL_RET4, HIXL_CHK_ACL_RET3, HIXL_CHK_ACL_RET2, HIXL_CHK_ACL_RET1)              \
  (__VA_ARGS__)

#define HIXL_CHK_ACL_BODY(expr, fmt, ...)                                                                              \
  do {                                                                                                                 \
    const aclError _ret = (expr);                                                                                      \
    if (_ret != ACL_SUCCESS) {                                                                                         \
      HIXL_REPORT_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", #expr, static_cast<uint32_t>(_ret));                    \
      HIXL_LOGE(FAILED, "Call acl api:%s failed, ret: 0x%X. " fmt, #expr, static_cast<uint32_t>(_ret), ##__VA_ARGS__); \
    }                                                                                                                  \
  } while (false)

#define HIXL_CHK_ACL1(expr) HIXL_CHK_ACL_BODY(expr, "")
#define HIXL_CHK_ACL2(expr, fmt) HIXL_CHK_ACL_BODY(expr, fmt)
#define HIXL_CHK_ACL3(expr, fmt, a) HIXL_CHK_ACL_BODY(expr, fmt, a)
#define HIXL_CHK_ACL4(expr, fmt, a, b) HIXL_CHK_ACL_BODY(expr, fmt, a, b)
#define HIXL_CHK_ACL5(expr, fmt, a, b, c) HIXL_CHK_ACL_BODY(expr, fmt, a, b, c)
#define HIXL_CHK_ACL6(expr, fmt, a, b, c, d) HIXL_CHK_ACL_BODY(expr, fmt, a, b, c, d)
#define HIXL_CHK_ACL7(expr, fmt, a, b, c, d, e) HIXL_CHK_ACL_BODY(expr, fmt, a, b, c, d, e)
#define HIXL_CHK_ACL8(expr, fmt, a, b, c, d, e, f) HIXL_CHK_ACL_BODY(expr, fmt, a, b, c, d, e, f)
#define HIXL_CHK_ACL_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
#define HIXL_CHK_ACL(...)                                                                                        \
  HIXL_CHK_ACL_GET_MACRO(__VA_ARGS__, HIXL_CHK_ACL8, HIXL_CHK_ACL7, HIXL_CHK_ACL6, HIXL_CHK_ACL5, HIXL_CHK_ACL4, \
                         HIXL_CHK_ACL3, HIXL_CHK_ACL2, HIXL_CHK_ACL1)                                            \
  (__VA_ARGS__)

#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_CHECKER_H_
