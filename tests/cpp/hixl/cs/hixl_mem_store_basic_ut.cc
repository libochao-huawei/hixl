/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <limits>
#include "gtest/gtest.h"
#include "hixl_mem_store.h"
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
void *IntToPtr(uint32_t addr) {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(addr));
}

void RecordMemoryRegions(HixlMemStore &store, const std::vector<std::pair<uint32_t, uint32_t>> &regions,
                         bool is_server) {
  for (const auto &region : regions) {
    void *addr = IntToPtr(region.first);
    EXPECT_EQ(store.RecordMemory(is_server, addr, region.second), SUCCESS);
  }
}
}  // anonymous namespace

TEST(HixlMemStoreBasicTest, RecordValidateAndUnrecord) {
  HixlMemStore store;
  uint32_t kServerDataAddr = 1;
  uint32_t kClientDataAddr = 2;
  void *saddr = &kServerDataAddr;
  void *caddr = &kClientDataAddr;
  constexpr uint32_t kBlockSizeBytes = 64;        // 远端数据块大小
  constexpr uint32_t kClientBufSizeBytes = 4096;  // 客户端缓冲区大小
  // 记录 server/client 内存
  EXPECT_EQ(store.RecordMemory(true, saddr, kClientBufSizeBytes), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, caddr, kClientBufSizeBytes), SUCCESS);

  // 校验访问
  EXPECT_EQ(store.ValidateMemoryAccess(saddr, kBlockSizeBytes, caddr), SUCCESS);

  // 重复记录同一server地址跳过记录
  EXPECT_EQ(store.RecordMemory(true, saddr, kClientBufSizeBytes), SUCCESS);
  // 重复记录同一client地址应返回参数错误
  EXPECT_EQ(store.RecordMemory(false, caddr, kClientBufSizeBytes), PARAM_INVALID);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, saddr), SUCCESS);
  EXPECT_EQ(store.UnrecordMemory(false, caddr), SUCCESS);

  // 再次注销应参数错误
  EXPECT_EQ(store.UnrecordMemory(true, saddr), PARAM_INVALID);
}

TEST(HixlMemStoreBasicTest, CheckMergedRegionsAccessTwoAdjacentRegions) {
  HixlMemStore store;
  std::vector<std::pair<uint32_t, uint32_t>> regions = {{100, 100}, {200, 100}};
  RecordMemoryRegions(store, regions, true);

  void *test_addr = IntToPtr(150);
  void *test_client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, test_client_addr, 100), SUCCESS);

  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, test_client_addr), SUCCESS);
}

TEST(HixlMemStoreBasicTest, CheckMergedRegionsAccessThreeAdjacentRegions) {
  HixlMemStore store;
  std::vector<std::pair<uint32_t, uint32_t>> regions = {{100, 100}, {200, 100}, {300, 100}};
  RecordMemoryRegions(store, regions, true);

  void *test_addr = IntToPtr(150);
  void *test_client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, test_client_addr, 100), SUCCESS);

  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, test_client_addr), SUCCESS);
}

TEST(HixlMemStoreBasicTest, CheckAccessAcrossGap) {
  HixlMemStore store;
  std::vector<std::pair<uint32_t, uint32_t>> regions = {{100, 100}, {300, 100}};
  RecordMemoryRegions(store, regions, true);

  void *test_addr = IntToPtr(150);
  void *test_client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, test_client_addr, 100), SUCCESS);

  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, test_client_addr), PARAM_INVALID);
}

TEST(HixlMemStoreBasicTest, CheckMergedRegionsAccessExceedMergedRange) {
  HixlMemStore store;
  std::vector<std::pair<uint32_t, uint32_t>> regions = {{100, 100}, {200, 100}};
  RecordMemoryRegions(store, regions, true);

  void *test_addr = IntToPtr(150);
  void *test_client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, test_client_addr, 100), SUCCESS);

  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 200, test_client_addr), PARAM_INVALID);
}

TEST(HixlMemStoreBasicTest, CheckMemoryForAccessOverflowDetected) {
  HixlMemStore store;
  // Register server memory near the maximum uintptr_t value (50 bytes from the top)
  uintptr_t near_max = std::numeric_limits<uintptr_t>::max() - 50;
  void *server_addr = reinterpret_cast<void *>(near_max);
  EXPECT_EQ(store.RecordMemory(true, server_addr, 50), SUCCESS);

  void *client_addr = reinterpret_cast<void *>(static_cast<uintptr_t>(100));
  EXPECT_EQ(store.RecordMemory(false, client_addr, 100), SUCCESS);

  // check_size=100 > uintptr_t::max - near_max=50, so overflow is detected -> PARAM_INVALID
  EXPECT_EQ(store.ValidateMemoryAccess(server_addr, 100, client_addr), PARAM_INVALID);
}

TEST(HixlMemStoreBasicTest, ValidateMemoryAccessBatchSingleDesc) {
  HixlMemStore store;
  void *server_addr = IntToPtr(100);
  void *client_addr = IntToPtr(200);
  constexpr size_t kSize = 1024;
  EXPECT_EQ(store.RecordMemory(true, server_addr, kSize), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize), SUCCESS);

  HixlOneSideOpDesc desc{server_addr, client_addr, 64};
  EXPECT_EQ(store.ValidateMemoryAccessBatch(1, &desc), SUCCESS);
}

TEST(HixlMemStoreBasicTest, ValidateMemoryAccessBatchMultipleDescsSameRegion) {
  HixlMemStore store;
  void *server_addr = IntToPtr(1000);
  void *client_addr = IntToPtr(2000);
  constexpr size_t kRegionSize = 8192;
  constexpr uint32_t kListNum = 100;
  EXPECT_EQ(store.RecordMemory(true, server_addr, kRegionSize), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kRegionSize), SUCCESS);

  std::vector<HixlOneSideOpDesc> descs(kListNum);
  for (uint32_t i = 0; i < kListNum; i++) {
    descs[i] = {IntToPtr(1000 + i * 8), IntToPtr(2000 + i * 8), 8};
  }
  EXPECT_EQ(store.ValidateMemoryAccessBatch(kListNum, descs.data()), SUCCESS);
}

TEST(HixlMemStoreBasicTest, ValidateMemoryAccessBatchUnregisteredAddr) {
  HixlMemStore store;
  void *server_addr = IntToPtr(1000);
  void *client_addr = IntToPtr(2000);
  constexpr size_t kRegionSize = 1024;
  EXPECT_EQ(store.RecordMemory(true, server_addr, kRegionSize), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kRegionSize), SUCCESS);

  HixlOneSideOpDesc desc{IntToPtr(5000), client_addr, 8};
  EXPECT_EQ(store.ValidateMemoryAccessBatch(1, &desc), PARAM_INVALID);
}

TEST(HixlMemStoreBasicTest, ValidateMemoryAccessBatchNullptrDescList) {
  HixlMemStore store;
  EXPECT_EQ(store.ValidateMemoryAccessBatch(1, nullptr), PARAM_INVALID);
}

TEST(HixlMemStoreBasicTest, ValidateMemoryAccessBatchZeroListNum) {
  HixlMemStore store;
  EXPECT_EQ(store.ValidateMemoryAccessBatch(0, nullptr), SUCCESS);
}

TEST(HixlMemStoreBasicTest, ValidateMemoryAccessBatchNullptrBuffers) {
  HixlMemStore store;
  void *server_addr = IntToPtr(1000);
  void *client_addr = IntToPtr(2000);
  EXPECT_EQ(store.RecordMemory(true, server_addr, 1024), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, 1024), SUCCESS);

  HixlOneSideOpDesc desc_null_remote{nullptr, client_addr, 8};
  EXPECT_EQ(store.ValidateMemoryAccessBatch(1, &desc_null_remote), PARAM_INVALID);

  HixlOneSideOpDesc desc_null_local{server_addr, nullptr, 8};
  EXPECT_EQ(store.ValidateMemoryAccessBatch(1, &desc_null_local), PARAM_INVALID);

  HixlOneSideOpDesc desc_zero_len{server_addr, client_addr, 0};
  EXPECT_EQ(store.ValidateMemoryAccessBatch(1, &desc_zero_len), PARAM_INVALID);
}

TEST(HixlMemStoreBasicTest, ValidateMemoryAccessBatchMergedRegions) {
  HixlMemStore store;
  std::vector<std::pair<uint32_t, uint32_t>> regions = {{100, 100}, {200, 100}};
  RecordMemoryRegions(store, regions, true);

  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, 200), SUCCESS);

  std::vector<HixlOneSideOpDesc> descs;
  descs.push_back({IntToPtr(150), client_addr, 100});
  descs.push_back({IntToPtr(100), IntToPtr(1), 64});
  EXPECT_EQ(store.ValidateMemoryAccessBatch(static_cast<uint32_t>(descs.size()), descs.data()), SUCCESS);
}
}  // namespace hixl