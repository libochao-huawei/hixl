/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_TYPES_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>

#include "acl/acl.h"
#include "hixl/hixl_types.h"

namespace hixl {
struct VaInfo {
  uintptr_t va_addr = 0;
  size_t len = 0;
  // True for ranges mapped through an ACL import, false for identity-bound local addresses. Only
  // ranges of the same kind are merged into one entry, because their local addresses come from disjoint
  // address spaces that can coincide.
  bool imported = false;
};

// Key: peer address; value: mapped address and length of one contiguous range. Adjacent ranges whose
// peer and local addresses are both adjacent share a single entry, so one transfer op needs one descriptor.
using UbMemRemoteIndex = std::multimap<uintptr_t, VaInfo>;

struct ShareHandleInfo {
  uintptr_t va_addr = 0;
  size_t len = 0;
  aclrtMemFabricHandle share_handle{};
  bool is_retained = false;
  MemType mem_type = MEM_DEVICE;
};

// Limits of the fabric memory pool and of the AICPU task streams, plus the parsed options shared by
// GlobalConfig and the engine option parsers.
constexpr size_t kMinUbMemCapacityTB = 1UL;
constexpr size_t kMaxUbMemCapacityTB = 1024UL;
constexpr size_t kMinUbMemStartAddrTB = 0UL;
constexpr size_t kMaxUbMemStartAddrTB = 1024UL;
constexpr size_t kMinTaskStreamNum = 1UL;
constexpr size_t kMaxTaskStreamNum = 8UL;

struct UbMemoryConfig {
  std::optional<size_t> max_capacity;
  std::optional<size_t> start_address;
  std::optional<size_t> task_stream_num;
  std::optional<bool> enable_aicpu_unfold;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_TYPES_H_
