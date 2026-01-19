/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <array>
#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

// 打开 private 仅用于 UT（你现有 UT 里也直接访问过 mem_store_）
#define private public
#define protected public
#include "hixl_cs_client.h"
#undef protected
#undef private

namespace hixl {
namespace {

constexpr uint32_t kUbDevId = 2U;
constexpr uint32_t kDummyPort = 12345U;

constexpr uint32_t kListNum1 = 1U;
constexpr uint64_t kLen8 = 8ULL;

constexpr uint64_t kFlagDone = 1ULL;
constexpr uint64_t kFlagInit = 0ULL;

EndpointDesc MakeUbDeviceSrcEp(CommProtocol protocol, uint32_t dev_id) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = dev_id;
  return ep;
}

EndpointDesc MakeUbDeviceDstEp(CommProtocol protocol, uint32_t dev_id) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = dev_id;
  return ep;
}

void PrepareUbInternalsForUt(HixlCSClient &cli, void *remote_flag_addr) {
  // 1) 跳过 EnsureUbRemoteFlagInitedLocked() 的 tag 查找
  cli.ub_remote_flag_inited_ = true;
  cli.ub_remote_flag_addr_ = remote_flag_addr;
  cli.ub_remote_flag_size_ = static_cast<uint64_t>(sizeof(uint64_t));

  // 2) 跳过 EnsureUbKernelLoadedLocked() 的加载流程
  cli.ub_kernel_loaded_ = true;

  // 只要非空即可，LaunchUbAndStageD2H_ 会检查 stub_func != nullptr
  static uint8_t kNonNullStubObj = 0U;
  cli.ub_stub_get_ = static_cast<const void *>(&kNonNullStubObj);
  cli.ub_stub_put_ = static_cast<const void *>(&kNonNullStubObj);
}

void RecordMemForBatchTransfer(HixlCSClient &cli, void *remote_addr, size_t remote_size, void *local_addr, size_t local_size) {
  // BatchTransfer() 在进入 UB / ROCE 前会做 mem_store_ 校验
  (void)cli.mem_store_.RecordMemory(true, remote_addr, remote_size);
  (void)cli.mem_store_.RecordMemory(false, local_addr, local_size);
}

}  // namespace

class HixlCSClientUbFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    const EndpointDesc src = MakeUbDeviceSrcEp(COMM_PROTOCOL_UBC_TP, kUbDevId);
    const EndpointDesc dst = MakeUbDeviceDstEp(COMM_PROTOCOL_UBC_TP, kUbDevId);

    const Status ret = cli_.Create("127.0.0.1", kDummyPort, &src, &dst);
    ASSERT_EQ(ret, SUCCESS);

    // UT 不走真实 Connect，这里给一个占位值（FillUbBatchArgs_ 会写入 args->channel）
    cli_.client_channel_handle_ = static_cast<ChannelHandle>(1ULL);

    // remote_flag 用 host 内存模拟（UT 里仅作为地址透传）
    remote_flag_ = kFlagInit;
    PrepareUbInternalsForUt(cli_, static_cast<void *>(&remote_flag_));
  }

  void TearDown() override {
    (void)cli_.Destroy();
  }

  HixlCSClient cli_{};
  uint64_t remote_flag_{0ULL};
};

TEST_F(HixlCSClientUbFixture, GetCompletePoolSingleton_SameObject) {
  CompletePool *p0 = &GetCompletePool();
  CompletePool *p1 = &GetCompletePool();
  EXPECT_EQ(p0, p1);
}

TEST_F(HixlCSClientUbFixture, SelectUbLists_GetAndPut_CorrectMapping) {
  std::array<void *, 1> dst_list{};
  std::array<const void *, 1> src_list{};
  std::array<uint64_t, 1> len_list{};

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = dst_list.data();
  mem.src_buf_list = src_list.data();
  mem.len_list = len_list.data();

  const void *local_list = nullptr;
  const void *remote_list = nullptr;

  {
    const Status ret = cli_.SelectUbLists(true, mem, &local_list, &remote_list);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(remote_list, static_cast<const void *>(mem.src_buf_list));
    EXPECT_EQ(local_list, static_cast<const void *>(mem.dst_buf_list));
  }

  local_list = nullptr;
  remote_list = nullptr;
  {
    const Status ret = cli_.SelectUbLists(false, mem, &local_list, &remote_list);
    EXPECT_EQ(ret, SUCCESS);
    EXPECT_EQ(local_list, static_cast<const void *>(mem.src_buf_list));
    EXPECT_EQ(remote_list, static_cast<const void *>(mem.dst_buf_list));
  }
}

TEST_F(HixlCSClientUbFixture, BatchPut_UBDevice_Success_CompleteAndRelease) {
  std::array<uint8_t, 8> local_src{};
  std::array<uint8_t, 8> remote_dst{};

  RecordMemForBatchTransfer(cli_,
                           static_cast<void *>(remote_dst.data()),
                           remote_dst.size(),
                           static_cast<void *>(local_src.data()),
                           local_src.size());

  void *remote_list[kListNum1] = { static_cast<void *>(remote_dst.data()) };
  const void *local_list[kListNum1] = { static_cast<const void *>(local_src.data()) };
  uint64_t len_list[kListNum1] = { kLen8 };

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = remote_list;   // put: dst=remote
  mem.src_buf_list = local_list;    // put: src=local
  mem.len_list = len_list;

  void *qh = nullptr;
  const uint32_t before_in_use = GetCompletePool().GetInUseCount();

  Status ret = cli_.BatchTransfer(false, mem, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  // 模拟 D2H 后完成：直接把 host_flag 写成 done
  auto *ub = static_cast<UbCompleteHandle *>(qh);
  ASSERT_NE(ub->slot.host_flag, nullptr);
  *(static_cast<uint64_t *>(ub->slot.host_flag)) = kFlagDone;

  int32_t status = -1;
  ret = cli_.CheckStatus(qh, &status);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(status, BatchTransferStatus::COMPLETED);

  const uint32_t after_in_use = GetCompletePool().GetInUseCount();
  EXPECT_EQ(after_in_use, before_in_use);
}

TEST_F(HixlCSClientUbFixture, BatchGet_UBDevice_Success_CompleteAndRelease) {
  std::array<uint8_t, 8> remote_src{};
  std::array<uint8_t, 8> local_dst{};

  RecordMemForBatchTransfer(cli_,
                           static_cast<void *>(remote_src.data()),
                           remote_src.size(),
                           static_cast<void *>(local_dst.data()),
                           local_dst.size());

  void *local_list[kListNum1] = { static_cast<void *>(local_dst.data()) };
  const void *remote_list[kListNum1] = { static_cast<const void *>(remote_src.data()) };
  uint64_t len_list[kListNum1] = { kLen8 };

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = local_list;    // get: dst=local
  mem.src_buf_list = remote_list;   // get: src=remote
  mem.len_list = len_list;

  void *qh = nullptr;
  const uint32_t before_in_use = GetCompletePool().GetInUseCount();

  Status ret = cli_.BatchTransfer(true, mem, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  auto *ub = static_cast<UbCompleteHandle *>(qh);
  ASSERT_NE(ub->slot.host_flag, nullptr);
  *(static_cast<uint64_t *>(ub->slot.host_flag)) = kFlagDone;

  int32_t status = -1;
  ret = cli_.CheckStatus(qh, &status);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(status, BatchTransferStatus::COMPLETED);

  const uint32_t after_in_use = GetCompletePool().GetInUseCount();
  EXPECT_EQ(after_in_use, before_in_use);
}

TEST_F(HixlCSClientUbFixture, BatchTransferUB_Fail_ListNumZero) {
  CommunicateMem mem{};
  mem.list_num = 0U;
  mem.dst_buf_list = nullptr;
  mem.src_buf_list = nullptr;
  mem.len_list = nullptr;

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(true, mem, &qh);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(qh, nullptr);
}

TEST_F(HixlCSClientUbFixture, Destroy_Fail_WhenUbSlotInUse_ThenRecover) {
  std::array<uint8_t, 8> local_src{};
  std::array<uint8_t, 8> remote_dst{};

  RecordMemForBatchTransfer(cli_,
                           static_cast<void *>(remote_dst.data()),
                           remote_dst.size(),
                           static_cast<void *>(local_src.data()),
                           local_src.size());

  void *remote_list[kListNum1] = { static_cast<void *>(remote_dst.data()) };
  const void *local_list[kListNum1] = { static_cast<const void *>(local_src.data()) };
  uint64_t len_list[kListNum1] = { kLen8 };

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = remote_list;
  mem.src_buf_list = local_list;
  mem.len_list = len_list;

  void *qh = nullptr;
  Status ret = cli_.BatchTransfer(false, mem, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  // 不完成时 Destroy 应失败（你 Destroy 里会检查 pool in_use）
  ret = cli_.Destroy();
  EXPECT_NE(ret, SUCCESS);

  // 现在完成并释放 slot，确保 TearDown 不再失败
  auto *ub = static_cast<UbCompleteHandle *>(qh);
  *(static_cast<uint64_t *>(ub->slot.host_flag)) = kFlagDone;

  int32_t status = -1;
  ret = cli_.CheckStatus(qh, &status);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(status, BatchTransferStatus::COMPLETED);

  // 再 Destroy 应成功
  ret = cli_.Destroy();
  EXPECT_EQ(ret, SUCCESS);

  // 避免 TearDown 再 Destroy 一次触发问题
  cli_.src_endpoint_.reset();
  cli_.is_device_ = false;
}

}  // namespace hixl
