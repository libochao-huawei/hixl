/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_PROFILING_PROF_TYPES_H_
#define CANN_HIXL_SRC_HIXL_PROFILING_PROF_TYPES_H_

#include <cstdint>

namespace hixl {
constexpr uint64_t kInvalidProfRange = UINT64_MAX;
// 9.2.0 is encoded as 90200000 by aclsysGetVersionNum. aclprof requires runtime > 9.2.0.
constexpr int32_t kAclProfRuntimeVersionThreshold = 90200000;

enum class HixlProfType {
  HixlOpBatchRead = 0,
  HixlOpBatchWrite = 1,
};

inline const char *GetProfName(const HixlProfType api_id) {
  return api_id == HixlProfType::HixlOpBatchRead ? "hixlOpBatchRead" : "hixlOpBatchWrite";
}
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_PROFILING_PROF_TYPES_H_
