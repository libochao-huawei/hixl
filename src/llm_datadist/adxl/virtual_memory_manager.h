/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef VIRTUAL_MEMORY_MANAGER_H
#define VIRTUAL_MEMORY_MANAGER_H

#include <cstdint>
#include <future>
#include <utility>
#include <vector>
#include <bitset>
#include <unordered_map>
#include "adxl/adxl_types.h"
#include "channel.h"
#include "control_msg_handler.h"
#include "acl/acl.h"

namespace adxl {
namespace {
constexpr size_t kGlobalVirtualMemorySize = 1024UL * 1024UL * 1024UL * 1024UL * 128UL;      // 128T
constexpr size_t kGlobalVirtualMemoryStartAddr = 1024UL * 1024UL * 1024UL * 1024UL * 40UL;  // 40T
constexpr size_t kBlockSize = 1024UL * 1024UL * 1024UL;  // 1GB
constexpr size_t kNumBlocks = 131072UL;                  // 128TB / 1GB = 131072 blocks
constexpr uint64_t kReserveFlagHugePage = 1UL;
}  // namespace
class VirtualMemoryManager {
 public:
  static VirtualMemoryManager &GetInstance();
  ~VirtualMemoryManager() = default;
  VirtualMemoryManager(const VirtualMemoryManager &) = delete;
  VirtualMemoryManager(const VirtualMemoryManager &&) = delete;
  VirtualMemoryManager &operator=(const VirtualMemoryManager &) = delete;
  VirtualMemoryManager &operator=(const VirtualMemoryManager &&) = delete;

  Status Initialize();
  void Finalize();
  Status ReserveMemory(size_t size, uintptr_t &mem_addr);
  Status ReleaseMemory(uintptr_t mem_addr);

 private:
  VirtualMemoryManager() = default;

  // Bitmap tracking (131,072 bits = 16KB)
  std::bitset<kNumBlocks> bitmap_;

  // Allocation metadata: start address -> block count
  std::unordered_map<uintptr_t, size_t> allocations_;

  // Initialization flag
  bool initialized_ = false;

  std::mutex global_virtual_memory_mutex_;
  void *global_virtual_memory_{};
  uintptr_t global_virtual_memory_addr_{};
};
}  // namespace adxl

#endif  // VIRTUAL_MEMORY_MANAGER_H
