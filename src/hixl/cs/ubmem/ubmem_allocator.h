/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ALLOCATOR_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>

#include "acl/acl.h"
#include "cs/ubmem/ubmem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {
class UbMemAllocator {
 public:
  // Allocates VMM memory without exporting. ExportToShareableHandle performs ACL export at most
  // once per allocation and caches the handle for later callers (including RegisterMem).
  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);
  // Returns true when va_addr is a live allocation from MallocMem (adxl::MallocMem aliases it).
  static bool IsAllocatedByMallocMem(uintptr_t va_addr);
  // First call exports; later calls return the cached handle (ACL export is once per allocation).
  static Status ExportToShareableHandle(uintptr_t va_addr, aclrtMemFabricHandle &share_handle);
  // Exports a foreign (not MallocMem) physical block at most once per process and caches the handle.
  // The key is the full block resolved by aclrtMemGetAddressRange, so user ranges that share one
  // physical block converge on the same key. The cache stores only the share handle bytes: it is not
  // a keep-alive reference, HIXL never releases an exported share handle explicitly, and the handle
  // stays valid as long as the backing physical memory is alive. Physical memory lifetime is owned by
  // the per-registration pa_handle retain/free pair of every caller. Entries are intentionally never
  // removed on deregistration: the fact that this process exported the block does not change.
  static Status GetOrExportForeignShareHandle(uintptr_t block_addr, size_t block_len, aclrtDrvMemHandle pa_handle,
                                              aclrtMemFabricHandle &share_handle);
  static Status GetPaHandleFromVa(uintptr_t va_addr, aclrtDrvMemHandle &pa_handle);
  // Ledger of the ranges this process exported. ACL shares a physical allocation with other processes
  // only, so a share handle can never be imported back into the process that exported it: the import
  // ownership check answers "was this range exported here", which is process-wide even though the export
  // records themselves are held per endpoint. The range identity is the same one used to pair an import
  // with its unimport: share handle bytes plus address and length.
  static void RecordExportedRange(const ShareHandleInfo &info);
  static void ReleaseExportedRange(const ShareHandleInfo &info);
  static bool IsRangeExportedHere(const ShareHandleInfo &info);
  static Status AllocatePhysicalMemory(MemType type, size_t total_size, int32_t logic_device_id,
                                       aclrtDrvMemHandle &handle);

 private:
  // Test only: clears the process-wide foreign share handle cache so tests start from an empty cache.
  // Not used by production paths; production entries are intentionally never removed on deregistration.
  static void ResetForeignShareHandleCacheForTest();
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_UBMEM_ALLOCATOR_H_
