/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "ascendcl_stub.h"
#include "cs/ubmem/ubmem_aicpu_param.h"
#include "cs/transfer_pool.h"
#include "depends/runtime/src/runtime_stub.h"
#include "engine/endpoint_test_utils.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
class StreamIdAclStub : public endpoint_test::MockAclRuntimeStub {
 public:
  aclError aclrtStreamGetId(aclrtStream stream, int32_t *stream_id) override {
    (void)stream;
    if (stream_id == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *stream_id = stream_id_;
    return ACL_SUCCESS;
  }

  int32_t stream_id_{7};
};

HixlOneSideOpDesc MakeOp(uintptr_t local_addr, uintptr_t remote_addr, uint64_t len) {
  HixlOneSideOpDesc desc{};
  desc.local_buf = reinterpret_cast<void *>(local_addr);
  desc.remote_buf = reinterpret_cast<void *>(remote_addr);
  desc.len = len;
  return desc;
}

TransferPool::SlotHandle MakeSlot() {
  TransferPool::SlotHandle slot{};
  slot.device_id = 3;
  slot.slot_index = 1U;
  slot.ubmem_stream = reinterpret_cast<aclrtStream>(0x1234);
  slot.thread = 0xABCDEFULL;
  slot.notify_id = 9U;
  return slot;
}
}  // namespace

class UbMemAicpuParamUt : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = std::make_shared<StreamIdAclStub>();
    acl_stub_->soc_name_ = "Ascend910_9391";
    acl_stub_->phy_device_id_ = 5;
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    SetStubRtStreamSqId(11U);
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
  }

  std::shared_ptr<StreamIdAclStub> acl_stub_;
};

TEST_F(UbMemAicpuParamUt, BuildWriteKeepsLocalAsSrc) {
  const HixlOneSideOpDesc src = MakeOp(0x1000U, 0x2000U, 64U);
  std::vector<UbMemAicpuTransferDesc> dst;
  ASSERT_EQ(BuildUbMemTransferDescs(false, &src, 1U, dst), SUCCESS);
  ASSERT_EQ(dst.size(), 1U);
  EXPECT_EQ(dst[0].src_addr, 0x1000U);
  EXPECT_EQ(dst[0].dst_addr, 0x2000U);
  EXPECT_EQ(dst[0].length, 64U);
}

TEST_F(UbMemAicpuParamUt, BuildReadSwapsSrcAndDst) {
  const HixlOneSideOpDesc src = MakeOp(0x1000U, 0x2000U, 32U);
  std::vector<UbMemAicpuTransferDesc> dst;
  ASSERT_EQ(BuildUbMemTransferDescs(true, &src, 1U, dst), SUCCESS);
  ASSERT_EQ(dst.size(), 1U);
  EXPECT_EQ(dst[0].src_addr, 0x2000U);
  EXPECT_EQ(dst[0].dst_addr, 0x1000U);
  EXPECT_EQ(dst[0].length, 32U);
}

TEST_F(UbMemAicpuParamUt, BuildSplitsOver4GbIntoTwoDescs) {
  const uint64_t over_4g = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U;
  const HixlOneSideOpDesc src = MakeOp(0x1000U, 0x2000U, over_4g);
  std::vector<UbMemAicpuTransferDesc> dst;
  ASSERT_EQ(BuildUbMemTransferDescs(false, &src, 1U, dst), SUCCESS);
  ASSERT_EQ(dst.size(), 2U);
  EXPECT_EQ(dst[0].length, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(dst[1].length, 1U);
  EXPECT_EQ(dst[1].src_addr, 0x1000UL + std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(dst[1].dst_addr, 0x2000UL + std::numeric_limits<uint32_t>::max());
}

TEST_F(UbMemAicpuParamUt, BuildRejectsNullEmptyAndZeroAddr) {
  std::vector<UbMemAicpuTransferDesc> dst;
  EXPECT_EQ(BuildUbMemTransferDescs(false, nullptr, 1U, dst), PARAM_INVALID);
  const HixlOneSideOpDesc src = MakeOp(0x1000U, 0x2000U, 8U);
  EXPECT_EQ(BuildUbMemTransferDescs(false, &src, 0U, dst), PARAM_INVALID);
  const HixlOneSideOpDesc zero_addr = MakeOp(0U, 0x2000U, 8U);
  EXPECT_EQ(BuildUbMemTransferDescs(false, &zero_addr, 1U, dst), PARAM_INVALID);
  const HixlOneSideOpDesc zero_len = MakeOp(0x1000U, 0x2000U, 0U);
  EXPECT_EQ(BuildUbMemTransferDescs(false, &zero_len, 1U, dst), PARAM_INVALID);
}

TEST_F(UbMemAicpuParamUt, FillKernelParamWritesRtsqAndNotify) {
  uint8_t desc_buf[sizeof(UbMemAicpuTransferDesc)] = {};
  const TransferPool::SlotHandle slot = MakeSlot();
  UbMemAicpuKernelParam param{};
  ASSERT_EQ(FillUbMemKernelParam(slot, desc_buf, 0U, 1U, false, true, 1500U, param), SUCCESS);
  EXPECT_EQ(param.desc_count, 1U);
  EXPECT_EQ(param.direction, static_cast<uint32_t>(UbMemAicpuTransferDirection::kWrite));
  EXPECT_EQ(param.timeout_ms, 1500U);
  EXPECT_EQ(param.notify_id, 9U);
  EXPECT_EQ(param.emit_notify_record, 1U);
  EXPECT_EQ(param.transfer_ctx_key, 0xABCDEFULL);
  EXPECT_EQ(param.version, kUbMemKernelParamVersion);
  EXPECT_EQ(param.device_id, 5U);
  EXPECT_EQ(param.rtsq_id, 11U);
  EXPECT_EQ(param.rtsq_stream_id, 7U);
  EXPECT_EQ(param.rtsq_logic_cq_id, 12U);
  EXPECT_EQ(param.desc_addr, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(desc_buf)));
}

TEST_F(UbMemAicpuParamUt, FillKernelParamReadOmitsNotifyAndAdvancesTaskId) {
  uint8_t desc_buf[sizeof(UbMemAicpuTransferDesc) * 2U] = {};
  const TransferPool::SlotHandle slot = MakeSlot();
  UbMemAicpuKernelParam first{};
  ASSERT_EQ(FillUbMemKernelParam(slot, desc_buf, 0U, 1U, true, false, 10U, first), SUCCESS);
  EXPECT_EQ(first.direction, static_cast<uint32_t>(UbMemAicpuTransferDirection::kRead));
  EXPECT_EQ(first.emit_notify_record, 0U);
  const uint32_t first_task_id = first.rtsq_task_id;

  UbMemAicpuKernelParam second{};
  ASSERT_EQ(FillUbMemKernelParam(slot, desc_buf, 1U, 1U, true, true, 10U, second), SUCCESS);
  EXPECT_EQ(second.rtsq_task_id, first_task_id + 1U);
  EXPECT_EQ(second.desc_addr,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(desc_buf + sizeof(UbMemAicpuTransferDesc))));
}

TEST_F(UbMemAicpuParamUt, FillKernelParamRejectsMissingContextAndStream) {
  uint8_t desc_buf[8] = {};
  TransferPool::SlotHandle slot = MakeSlot();
  UbMemAicpuKernelParam param{};
  EXPECT_EQ(FillUbMemKernelParam(slot, nullptr, 0U, 1U, false, false, 1U, param), PARAM_INVALID);
  EXPECT_EQ(FillUbMemKernelParam(slot, desc_buf, 0U, 0U, false, false, 1U, param), PARAM_INVALID);
  slot.thread = 0U;
  EXPECT_EQ(FillUbMemKernelParam(slot, desc_buf, 0U, 1U, false, false, 1U, param), PARAM_INVALID);
  slot.thread = 1U;
  slot.ubmem_stream = nullptr;
  EXPECT_NE(FillUbMemKernelParam(slot, desc_buf, 0U, 1U, false, false, 1U, param), SUCCESS);
}

TEST_F(UbMemAicpuParamUt, FillKernelParamFailsWhenPhyDevLookupFails) {
  uint8_t desc_buf[8] = {};
  acl_stub_->phy_dev_failed_ = true;
  UbMemAicpuKernelParam param{};
  EXPECT_EQ(FillUbMemKernelParam(MakeSlot(), desc_buf, 0U, 1U, false, false, 1U, param), UNSUPPORTED);
}
}  // namespace hixl
