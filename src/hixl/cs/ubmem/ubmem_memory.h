/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_MEMORY_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_MEMORY_H_

#include <memory>
#include <vector>

#include "acl/acl.h"
#include "cs/ubmem/ubmem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {

// Endpoint-local pairing of registered memory and peer mappings. Import decides internally whether a
// peer handle names memory this endpoint exported; those ranges are identity-bound, while all foreign
// ranges are imported and mapped.
class UbMemMemory {
 public:
  UbMemMemory();
  ~UbMemMemory();
  UbMemMemory(const UbMemMemory &) = delete;
  UbMemMemory &operator=(const UbMemMemory &) = delete;
  UbMemMemory(UbMemMemory &&) = delete;
  UbMemMemory &operator=(UbMemMemory &&) = delete;

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  Status GetShareHandles(MemHandle mem_handle, std::vector<ShareHandleInfo> &out) const;
  Status Import(const std::vector<ShareHandleInfo> &handles);
  Status Unimport(const std::vector<ShareHandleInfo> &handles);
  void Finalize();
  std::shared_ptr<const UbMemRemoteIndex> GetTranslationIndex() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_MEMORY_H_
