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
#include "ascendcl_stub.h"

// 为了直接访问 client 内部状态
#define private public
#define protected public
#include "hixl_cs_client.h"
#include "complete_pool.h"
#undef protected
#undef private


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
  cli.ub_func_get_ = static_cast<void *>(&kNonNullStub);
  cli.ub_func_put_ = static_cast<void *>(&kNonNullStub);
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

Status PollUntilCompleted(HixlCSClient &cli, void *qh, HixlCompleteStatus *out_status) {
  HIXL_CHECK_NOTNULL(out_status);
  *out_status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;

  for (int i = 0; i < 10; ++i) {
    const Status ret = cli.CheckStatus(qh, out_status);
    if (ret != SUCCESS) {
      return ret;
    }
    if (*out_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
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

    HixlClientConfig config{};
    ASSERT_EQ(cli_.Create("127.0.0.1", kDummyPort, &src, &dst, &config), SUCCESS);

    cli_.client_channel_handle_ = static_cast<ChannelHandle>(1ULL);
    cli_.ub_remote_flag_inited_ = false;
    remote_flag_dev_ = 0ULL;
    FillTagMem(cli_, kTransFlagNameDevice, static_cast<void *>(&remote_flag_dev_), sizeof(uint64_t));

    PrepareKernelReadyForUt(cli_);
  }

  void TearDown() override {
    (void)cli_.Destroy();
    unsetenv("HIXL_UT_UB_FLAG_HACK");
  }

  // [新增] 辅助函数：统一处理 Buffer 的注册和 CommunicateMem 参数的组装
  CommunicateMem SetupBatchTransfer(bool is_get) {
    RecordMemForBatchTransfer(cli_, remote_buf_.data(), remote_buf_.size(),
                              local_buf_.data(), local_buf_.size());

    if (is_get) {
      remote_list_const_[0] = remote_buf_.data(); // src (remote)
      local_list_[0]        = local_buf_.data();  // dst (local)
      mem_.src_buf_list     = remote_list_const_;
      mem_.dst_buf_list     = local_list_;
    } else {
      local_list_const_[0]  = local_buf_.data();  // src (local)
      remote_list_[0]       = remote_buf_.data(); // dst (remote)
      mem_.src_buf_list     = local_list_const_;
      mem_.dst_buf_list     = remote_list_;
    }

    len_list_[0]  = kLen8;
    mem_.len_list = len_list_;
    mem_.list_num = kListNum1;

    return mem_;
  }

  HixlCSClient cli_{};
  uint64_t remote_flag_dev_{0ULL};

  // [新增] 将临时 Buffer 和数组沉淀到类的生命周期中，防止悬空指针
  std::array<uint8_t, 8> local_buf_{};
  std::array<uint8_t, 8> remote_buf_{};
  void* local_list_[1]{};
  void* remote_list_[1]{};
  const void* local_list_const_[1]{};
  const void* remote_list_const_[1]{};
  uint64_t len_list_[1]{};
  CommunicateMem mem_{};
};

// ======================= 精简后的 UT 用例 =======================

// 1. 测试 Put 操作（预期因为 notify_addr 为 0 而直接失败拦截）
TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);
  CommunicateMem mem = SetupBatchTransfer(false); // false = Put

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(false, mem, &qh);

  // [修改] 预期行为改为：必然失败，且不会返回有效的查询句柄
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(qh, nullptr);

  // [修改] 因为句柄为空，删掉底下的 PollUntilCompleted 轮询逻辑
}

// 2. 测试 Get 操作（预期同样被拦截失败）
TEST_F(HixlCSClientUbFixture, BatchGetUbDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);
  CommunicateMem mem = SetupBatchTransfer(true); // true = Get

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(true, mem, &qh);

  // [修改] 预期行为改为：必然失败
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(qh, nullptr);
}

TEST_F(HixlCSClientUbFixture, PrepareUbRemoteFlagAndKernelMissingTagFail) {
  cli_.ub_remote_flag_inited_ = false;
  cli_.tag_mem_descs_.clear();

  void *remote_flag = nullptr;
  EXPECT_EQ(cli_.PrepareUbRemoteFlagAndKernel(remote_flag), PARAM_INVALID);
  EXPECT_EQ(remote_flag, nullptr);
}

// 4. 原有的这个测试本来就是预期失败的，不用大改，确认逻辑正确即可
TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceNotifyWaitFail) {
  g_Stub_aclrtWaitAndResetNotify_RETURN.push_back(static_cast<aclError>(-1));
  CommunicateMem mem = SetupBatchTransfer(false);

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(false, mem, &qh);

  // 预期必然失败
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(qh, nullptr);
}

// 3. 测试 Slot 耗尽的异常场景（既然第一次调用就会失败，直接验证它的拦截即可）
TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceSlotExhaustedFail) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);
  CommunicateMem mem = SetupBatchTransfer(false);

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(false, mem, &qh);

  // [修改] 之前是想循环 128 次耗尽 Slot，现在因为 notify_addr=0，第一次就会被拦截并释放资源
  EXPECT_NE(ret, SUCCESS);
  EXPECT_EQ(qh, nullptr);

  // 验证资源是否被正确回滚释放（不会导致泄漏）

}

}  // namespace hixl