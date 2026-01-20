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
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vector>

#include "gtest/gtest.h"

// 为了直接访问 client 内部状态（保持你之前 UT 的方式）
#define private public
#define protected public
#include "hixl_cs_client.h"
#include "complete_pool.h"
#undef protected
#undef private

// 依赖你现有的 rtNotifyWait 打桩返回栈：ADD_STUB_RETURN_VALUE(rtNotifyWait, rtError_t);
extern std::vector<rtError_t> g_Stub_rtNotifyWait_RETURN;

namespace hixl {
namespace {

constexpr uint32_t kUbDevId = 2U;
constexpr uint32_t kDummyPort = 12345U;

constexpr uint32_t kListNum1 = 1U;
constexpr uint64_t kLen8 = 8ULL;

constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";

EndpointDesc MakeUbDeviceEp(CommProtocol protocol, uint32_t dev_id) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = dev_id;
  return ep;
}

void PrepareKernelReadyForUt(HixlCSClient &cli) {
  cli.ub_kernel_loaded_ = true;
  static uint8_t kNonNullStub = 0U;
  cli.ub_stub_get_ = static_cast<const void *>(&kNonNullStub);
  cli.ub_stub_put_ = static_cast<const void *>(&kNonNullStub);
}

void RecordMemForBatchTransfer(HixlCSClient &cli, void *remote_addr, size_t remote_size, void *local_addr,
                               size_t local_size) {
  (void)cli.mem_store_.RecordMemory(true, remote_addr, remote_size);
  (void)cli.mem_store_.RecordMemory(false, local_addr, local_size);
}

void FillTagMem(HixlCSClient &cli, const char *tag, void *addr, uint64_t size) {
  HcommMem mem{};
  mem.type = HCCL_MEM_TYPE_DEVICE;
  mem.addr = addr;
  mem.size = size;
  cli.tag_mem_descs_[tag] = mem;
}

Status PollUntilCompleted(HixlCSClient &cli, void *qh, int32_t *out_status) {
  HIXL_CHECK_NOTNULL(out_status);
  *out_status = -1;

  // 允许 poll 几次（兼容桩“异步语义”）
  for (int i = 0; i < 10; ++i) {
    const Status ret = cli.CheckStatus(qh, out_status);
    if (ret != SUCCESS) {
      return ret;
    }
    if (*out_status == BatchTransferStatus::COMPLETED) {
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return SUCCESS;
}

}  // namespace

class HixlCSClientUbFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    const EndpointDesc src = MakeUbDeviceEp(COMM_PROTOCOL_UBC_TP, kUbDevId);
    const EndpointDesc dst = MakeUbDeviceEp(COMM_PROTOCOL_UBC_TP, kUbDevId);

    const Status ret = cli_.Create("127.0.0.1", kDummyPort, &src, &dst);
    ASSERT_EQ(ret, SUCCESS);

    // 不走 Connect，全链路里只要 channel_handle 非空即可
    cli_.client_channel_handle_ = static_cast<ChannelHandle>(1ULL);

    // 让 PrepareUbRemoteFlagAndKernel 走 tag_mem_descs_ 的路径
    cli_.ub_remote_flag_inited_ = false;
    remote_flag_dev_ = 0ULL;
    FillTagMem(cli_, kTransFlagNameDevice, static_cast<void *>(&remote_flag_dev_), sizeof(uint64_t));

    PrepareKernelReadyForUt(cli_);
  }

  void TearDown() override {
    (void)cli_.Destroy();
    unsetenv("HIXL_UT_UB_FLAG_HACK");
  }

  HixlCSClient cli_{};
  uint64_t remote_flag_dev_{0ULL};
};

TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);

  std::array<uint8_t, 8> local_src{};
  std::array<uint8_t, 8> remote_dst{};

  RecordMemForBatchTransfer(cli_, static_cast<void *>(remote_dst.data()), remote_dst.size(),
                            static_cast<void *>(local_src.data()), local_src.size());

  void *remote_list[kListNum1] = {static_cast<void *>(remote_dst.data())};
  const void *local_list[kListNum1] = {static_cast<const void *>(local_src.data())};
  uint64_t len_list[kListNum1] = {kLen8};

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = remote_list;  // put: dst=remote
  mem.src_buf_list = local_list;   // put: src=local
  mem.len_list = len_list;

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(false, mem, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  int32_t st = -1;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, BatchTransferStatus::COMPLETED);
}

TEST_F(HixlCSClientUbFixture, BatchGetUbDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);

  std::array<uint8_t, 8> local_dst{};
  std::array<uint8_t, 8> remote_src{};

  RecordMemForBatchTransfer(cli_, static_cast<void *>(remote_src.data()), remote_src.size(),
                            static_cast<void *>(local_dst.data()), local_dst.size());

  const void *remote_list[kListNum1] = {static_cast<const void *>(remote_src.data())};
  void *local_list[kListNum1] = {static_cast<void *>(local_dst.data())};
  uint64_t len_list[kListNum1] = {kLen8};

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = local_list;   // get: dst=local
  mem.src_buf_list = remote_list;  // get: src=remote
  mem.len_list = len_list;

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(true, mem, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  int32_t st = -1;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, BatchTransferStatus::COMPLETED);
}

TEST_F(HixlCSClientUbFixture, PrepareUbRemoteFlagAndKernelMissingTagFail) {
  cli_.ub_remote_flag_inited_ = false;
  cli_.tag_mem_descs_.clear();

  void *remote_flag = nullptr;
  const Status ret = cli_.PrepareUbRemoteFlagAndKernel(&remote_flag);

  EXPECT_EQ(ret, PARAM_INVALID);
  EXPECT_EQ(remote_flag, nullptr);
}

TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceNotifyWaitFail) {
  // 这个用 return 栈控制失败，不需要改桩代码
  // 注意：GET_STUB_RETURN_VALUE 是 pop_back，所以 push 的顺序就是实际返回顺序
  g_Stub_rtNotifyWait_RETURN.push_back(static_cast<rtError_t>(-1));

  std::array<uint8_t, 8> local_src{};
  std::array<uint8_t, 8> remote_dst{};

  RecordMemForBatchTransfer(cli_, static_cast<void *>(remote_dst.data()), remote_dst.size(),
                            static_cast<void *>(local_src.data()), local_src.size());

  void *remote_list[kListNum1] = {static_cast<void *>(remote_dst.data())};
  const void *local_list[kListNum1] = {static_cast<const void *>(local_src.data())};
  uint64_t len_list[kListNum1] = {kLen8};

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = remote_list;
  mem.src_buf_list = local_list;
  mem.len_list = len_list;

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(false, mem, &qh);
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(qh, nullptr);
}

TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceSlotExhaustedFail) {
  // 目的：不调用 CheckStatus 让 slot 一直 in_use，直到池耗尽，然后下一次 BatchTransfer 失败
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);

  std::array<uint8_t, 8> local_src{};
  std::array<uint8_t, 8> remote_dst{};

  RecordMemForBatchTransfer(cli_, static_cast<void *>(remote_dst.data()), remote_dst.size(),
                            static_cast<void *>(local_src.data()), local_src.size());

  void *remote_list[kListNum1] = {static_cast<void *>(remote_dst.data())};
  const void *local_list[kListNum1] = {static_cast<const void *>(local_src.data())};
  uint64_t len_list[kListNum1] = {kLen8};

  CommunicateMem mem{};
  mem.list_num = kListNum1;
  mem.dst_buf_list = remote_list;
  mem.src_buf_list = local_list;
  mem.len_list = len_list;

  std::vector<void *> handles;
  handles.reserve(CompletePool::kMaxSlots);

  for (uint32_t i = 0; i < CompletePool::kMaxSlots; ++i) {
    void *qh = nullptr;
    const Status ret = cli_.BatchTransfer(false, mem, &qh);
    ASSERT_EQ(ret, SUCCESS);
    ASSERT_NE(qh, nullptr);
    handles.emplace_back(qh);
  }

  // 这次应该拿不到 slot（Acquire 返回 RESOURCE_EXHAUSTED，client 里会转成 FAILED 返回）
  void *qh_extra = nullptr;
  const Status ret_extra = cli_.BatchTransfer(false, mem, &qh_extra);
  EXPECT_NE(ret_extra, SUCCESS);
  EXPECT_EQ(qh_extra, nullptr);

  // 清理：把之前占用的 slot 全部释放（靠 memcpy hack 使 host_flag=1）
  for (void *h : handles) {
    int32_t st = -1;
    (void)PollUntilCompleted(cli_, h, &st);
    EXPECT_EQ(st, BatchTransferStatus::COMPLETED);
  }
}

}  // namespace hixl
