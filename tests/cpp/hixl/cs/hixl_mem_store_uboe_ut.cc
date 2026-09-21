/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "gtest/gtest.h"
#include "hixl_mem_store.h"
#include "hixl/hixl_types.h"
#include "cs/hixl_cs.h"

namespace hixl {
namespace {
void *IntToPtr(uint32_t addr) {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(addr));
}
}  // anonymous namespace

// 测试 RecordMemory 的 is_host_mem 和 register_dev_addr 参数
TEST(HixlMemStoreUboeTest, RecordHostMemoryWithDeviceAddr) {
  HixlMemStore store;
  void *host_addr = IntToPtr(100);
  void *dev_addr = IntToPtr(200);
  void *client_addr = IntToPtr(300);
  constexpr size_t kSize = 1024;

  // 记录 host 内存及其设备地址
  EXPECT_EQ(store.RecordMemory(true, host_addr, kSize, true, dev_addr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize, false, nullptr), SUCCESS);

  // 验证 BatchConvertHostAddr 可以正确转换
  HixlOneSideOpDesc desc_list[] = {{host_addr, client_addr, kSize}};
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), SUCCESS);
  EXPECT_EQ(desc_list[0].remote_buf, dev_addr);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, host_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, RecordDeviceMemoryWithoutDeviceAddr) {
  HixlMemStore store;
  void *dev_addr = IntToPtr(100);
  void *client_addr = IntToPtr(200);
  constexpr size_t kSize = 1024;

  // 记录设备内存（is_host_mem = false）
  EXPECT_EQ(store.RecordMemory(true, dev_addr, kSize, false, nullptr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize, false, nullptr), SUCCESS);

  // 验证 BatchConvertHostAddr 不转换设备内存
  HixlOneSideOpDesc desc_list[] = {{dev_addr, client_addr, kSize}};
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), SUCCESS);
  EXPECT_EQ(desc_list[0].remote_buf, dev_addr);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, dev_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, RecordMemoryWithNullptrDeviceAddr) {
  HixlMemStore store;
  void *host_addr = IntToPtr(100);
  void *client_addr = IntToPtr(200);
  constexpr size_t kSize = 1024;

  // 记录 host 内存，但设备地址为 nullptr
  EXPECT_EQ(store.RecordMemory(true, host_addr, kSize, true, nullptr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize, false, nullptr), SUCCESS);

  // 验证 BatchConvertHostAddr 会检测到 nullptr register_dev_addr
  HixlOneSideOpDesc desc_list[] = {{host_addr, client_addr, kSize}};
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), PARAM_INVALID);

  // 注销
  EXPECT_EQ(store.UnrecordMemory(true, host_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, CheckRegionsContiguousWithDeviceAddr) {
  HixlMemStore store;
  // 记录两个连续的 host 内存区域，它们的设备地址也是连续的
  void *host_addr1 = IntToPtr(100);
  void *host_addr2 = IntToPtr(200);  // 连续：100 + 100 = 200
  void *dev_addr1 = IntToPtr(1000);
  void *dev_addr2 = IntToPtr(1100);  // 连续：1000 + 100 = 1100
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, host_addr1, kSize, true, dev_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, host_addr2, kSize, true, dev_addr2), SUCCESS);

  // 验证访问跨越这两个区域的地址
  void *test_addr = IntToPtr(150);  // 在第一个区域内
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize), SUCCESS);

  // 访问成功（两个区域都连续）
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 50, client_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, CheckRegionsNonContiguousWithDeviceAddr) {
  HixlMemStore store;
  // 记录两个 host 内存区域，地址连续但设备地址不连续
  void *host_addr1 = IntToPtr(100);
  void *host_addr2 = IntToPtr(200);  // 连续
  void *dev_addr1 = IntToPtr(1000);
  void *dev_addr2 = IntToPtr(1200);  // 不连续：1000 + 100 != 1200
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, host_addr1, kSize, true, dev_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, host_addr2, kSize, true, dev_addr2), SUCCESS);

  // 尝试访问跨越这两个区域的地址应该失败
  void *test_addr = IntToPtr(150);
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);

  // 因为设备地址不连续，访问失败
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, client_addr), PARAM_INVALID);
}

TEST(HixlMemStoreUboeTest, CheckRegionsContiguousWithoutDeviceAddr) {
  HixlMemStore store;
  // 记录两个连续的内存区域，没有设备地址
  void *addr1 = IntToPtr(100);
  void *addr2 = IntToPtr(200);
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, addr1, kSize, false, nullptr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr2, kSize, false, nullptr), SUCCESS);

  // 验证访问跨越这两个区域的地址
  void *test_addr = IntToPtr(150);
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);

  // 成功，因为没有设备地址时，只要地址连续即可
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, client_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, CheckRegionsNonContiguousAddr) {
  HixlMemStore store;
  // 记录两个不连续的内存区域
  void *addr1 = IntToPtr(100);
  void *addr2 = IntToPtr(300);  // 不连续：100 + 100 != 300
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, addr1, kSize), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr2, kSize), SUCCESS);

  // 尝试访问跨越这两个区域的地址应该失败
  void *test_addr = IntToPtr(150);
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);

  // 失败，因为地址不连续
  EXPECT_EQ(store.ValidateMemoryAccess(test_addr, 100, client_addr), PARAM_INVALID);
}

TEST(HixlMemStoreUboeTest, MultipleRegionsWithMixedDeviceAddr) {
  HixlMemStore store;
  // 混合场景：有些有设备地址，有些没有
  void *addr1 = IntToPtr(100);
  void *addr2 = IntToPtr(200);  // 与 addr1 连续
  void *addr3 = IntToPtr(300);  // 与 addr2 连续
  void *dev_addr1 = IntToPtr(1000);
  void *dev_addr2 = IntToPtr(1100);  // 与 dev_addr1 连续
  // addr3 没有设备地址
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, addr1, kSize, true, dev_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr2, kSize, true, dev_addr2), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, addr3, kSize, false, nullptr), SUCCESS);

  // 访问跨越 addr1 和 addr2 应该成功
  void *client_addr = IntToPtr(1);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2), SUCCESS);
  EXPECT_EQ(store.ValidateMemoryAccess(IntToPtr(150), kSize, client_addr), SUCCESS);
}

TEST(HixlMemStoreUboeTest, BatchConvertHostAddrSuccess) {
  HixlMemStore store;
  void *host_addr1 = IntToPtr(100);
  void *host_addr2 = IntToPtr(200);
  void *dev_addr1 = IntToPtr(1000);
  void *dev_addr2 = IntToPtr(1100);
  void *client_host1 = IntToPtr(300);
  void *client_host2 = IntToPtr(400);
  void *client_dev1 = IntToPtr(2000);
  void *client_dev2 = IntToPtr(2100);
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, host_addr1, kSize, true, dev_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, host_addr2, kSize, true, dev_addr2), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_host1, kSize, true, client_dev1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_host2, kSize, true, client_dev2), SUCCESS);

  HixlOneSideOpDesc desc_list[] = {
      {host_addr1, client_host1, kSize},
      {host_addr2, client_host2, kSize},
  };
  EXPECT_EQ(store.BatchConvertHostAddr(2, desc_list), SUCCESS);
  EXPECT_EQ(desc_list[0].remote_buf, dev_addr1);
  EXPECT_EQ(desc_list[0].local_buf, client_dev1);
  EXPECT_EQ(desc_list[1].remote_buf, dev_addr2);
  EXPECT_EQ(desc_list[1].local_buf, client_dev2);
}

TEST(HixlMemStoreUboeTest, BatchConvertHostAddrWithOffset) {
  HixlMemStore store;
  void *host_base = IntToPtr(100);
  void *dev_base = IntToPtr(1000);
  void *client_host_base = IntToPtr(300);
  void *client_dev_base = IntToPtr(2000);
  constexpr size_t kSize = 200;

  EXPECT_EQ(store.RecordMemory(true, host_base, kSize, true, dev_base), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_host_base, kSize, true, client_dev_base), SUCCESS);

  void *host_offset = IntToPtr(150);
  void *client_offset = IntToPtr(350);
  HixlOneSideOpDesc desc_list[] = {
      {host_offset, client_offset, 50},
  };
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), SUCCESS);
  EXPECT_EQ(desc_list[0].remote_buf, IntToPtr(1050));
  EXPECT_EQ(desc_list[0].local_buf, IntToPtr(2050));
}

TEST(HixlMemStoreUboeTest, BatchConvertHostAddrDeviceMemUnchanged) {
  HixlMemStore store;
  void *dev_addr = IntToPtr(100);
  void *client_dev_addr = IntToPtr(200);
  constexpr size_t kSize = 100;

  EXPECT_EQ(store.RecordMemory(true, dev_addr, kSize, false, nullptr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_dev_addr, kSize, false, nullptr), SUCCESS);

  HixlOneSideOpDesc desc_list[] = {
      {dev_addr, client_dev_addr, kSize},
  };
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), SUCCESS);
  EXPECT_EQ(desc_list[0].remote_buf, dev_addr);
  EXPECT_EQ(desc_list[0].local_buf, client_dev_addr);
}

TEST(HixlMemStoreUboeTest, BatchConvertHostAddrUnregisteredRemote) {
  HixlMemStore store;
  void *client_addr = IntToPtr(200);
  constexpr size_t kSize = 100;
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize, false, nullptr), SUCCESS);

  HixlOneSideOpDesc desc_list[] = {
      {IntToPtr(999), client_addr, kSize},
  };
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), FAILED);
}

TEST(HixlMemStoreUboeTest, BatchConvertHostAddrUnregisteredLocal) {
  HixlMemStore store;
  void *server_addr = IntToPtr(100);
  constexpr size_t kSize = 100;
  EXPECT_EQ(store.RecordMemory(true, server_addr, kSize, false, nullptr), SUCCESS);

  HixlOneSideOpDesc desc_list[] = {
      {server_addr, IntToPtr(999), kSize},
  };
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), FAILED);
}

// Host映射区与Device区相邻时，跨属性边界的descriptor应被拒绝
TEST(HixlMemStoreUboeTest, BatchValidateMemoryAccessHostToDeviceBoundary) {
  HixlMemStore store;
  // Host映射区[100,200)，Device区[200,300)紧邻
  void *host_addr = IntToPtr(100);
  void *dev_region = IntToPtr(200);
  void *dev_map_addr = IntToPtr(1000);
  void *client_addr = IntToPtr(3000);
  constexpr size_t kSize = 100;
  EXPECT_EQ(store.RecordMemory(true, host_addr, kSize, true, dev_map_addr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, dev_region, kSize, false, nullptr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2, false, nullptr), SUCCESS);

  // descriptor跨界：remote [150, 250)，内存类型不同不可合并，应拒绝
  HixlOneSideOpDesc cross_desc[] = {{IntToPtr(150), client_addr, kSize}};
  EXPECT_EQ(store.BatchValidateMemoryAccess(1, cross_desc), PARAM_INVALID);

  // 对照：descriptor完整落在Host映射区内应成功
  HixlOneSideOpDesc host_desc[] = {{IntToPtr(150), client_addr, kSize / 2}};
  EXPECT_EQ(store.BatchValidateMemoryAccess(1, host_desc), SUCCESS);

  // 对照：descriptor完整落在Device区内应成功
  HixlOneSideOpDesc dev_desc[] = {{IntToPtr(250), client_addr, kSize / 2}};
  EXPECT_EQ(store.BatchValidateMemoryAccess(1, dev_desc), SUCCESS);
}

// Device区与Host映射区相邻（顺序反转），跨界descriptor同样应被拒绝
TEST(HixlMemStoreUboeTest, BatchValidateMemoryAccessDeviceToHostBoundary) {
  HixlMemStore store;
  // Device区[100,200)，Host映射区[200,300)紧邻
  void *dev_region = IntToPtr(100);
  void *host_addr = IntToPtr(200);
  void *dev_map_addr = IntToPtr(2000);
  void *client_addr = IntToPtr(3000);
  constexpr size_t kSize = 100;
  EXPECT_EQ(store.RecordMemory(true, dev_region, kSize, false, nullptr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, host_addr, kSize, true, dev_map_addr), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize * 2, false, nullptr), SUCCESS);

  // descriptor跨界：remote [150, 250)，应拒绝
  HixlOneSideOpDesc cross_desc[] = {{IntToPtr(150), client_addr, kSize}};
  EXPECT_EQ(store.BatchValidateMemoryAccess(1, cross_desc), PARAM_INVALID);
}

// 跨两个dev地址连续Host映射区的descriptor：校验通过且转换正确（保留合法合并场景）
TEST(HixlMemStoreUboeTest, BatchConvertHostAddrSpanContiguousHostRegions) {
  HixlMemStore store;
  // 两个Host映射区addr连续（100/200），dev地址也连续（1000/1100）
  void *host_addr1 = IntToPtr(100);
  void *host_addr2 = IntToPtr(200);
  void *dev_map_addr1 = IntToPtr(1000);
  void *dev_map_addr2 = IntToPtr(1100);
  void *client_addr = IntToPtr(3000);
  constexpr size_t kSize = 100;
  EXPECT_EQ(store.RecordMemory(true, host_addr1, kSize, true, dev_map_addr1), SUCCESS);
  EXPECT_EQ(store.RecordMemory(true, host_addr2, kSize, true, dev_map_addr2), SUCCESS);
  EXPECT_EQ(store.RecordMemory(false, client_addr, kSize, false, nullptr), SUCCESS);

  // descriptor跨两区：remote [150, 250)，校验通过，转换后remote应落在 [1050, 1150)
  HixlOneSideOpDesc desc_list[] = {{IntToPtr(150), client_addr, kSize}};
  EXPECT_EQ(store.BatchValidateMemoryAccess(1, desc_list), SUCCESS);
  EXPECT_EQ(store.BatchConvertHostAddr(1, desc_list), SUCCESS);
  EXPECT_EQ(desc_list[0].remote_buf, IntToPtr(1050));
}

}  // namespace hixl
