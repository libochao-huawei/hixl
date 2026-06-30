/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>
#include "gtest/gtest.h"
#include "depends/hccl/src/hccl_stub.h"
#include "hccl/hccl_types.h"
#include "proxy/hcomm_proxy.h"

namespace hixl {
namespace {

class HcommProxyTransferRetryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetBasicTransferStats();
  }
  void TearDown() override {
    ResetBasicTransferStats();
  }
};

// 返回 HCCL_E_AGAIN 若干次后成功，代理层在超时窗口内重试并最终成功。
TEST_F(HcommProxyTransferRetryTest, ReadRetriesThenSucceeds) {
  constexpr uint32_t kAgainTimes = 3U;
  SetBasicTransferAgainCount(kAgainTimes);
  uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t dst[8] = {0};

  int32_t ret = HcommProxy::ReadOnThread(0, 0, dst, src, sizeof(src), 1000);
  EXPECT_EQ(ret, HCCL_SUCCESS);
  EXPECT_EQ(GetBasicTransferCallCount(), kAgainTimes + 1U);
  EXPECT_EQ(dst[0], src[0]);
}

// 持续返回 HCCL_E_AGAIN，超过超时时间后停止重试并返回 HCCL_E_AGAIN。
TEST_F(HcommProxyTransferRetryTest, ReadStopsOnTimeout) {
  SetBasicTransferAgainCount(1000000U);
  uint8_t src[8] = {1};
  uint8_t dst[8] = {0};

  int32_t ret = HcommProxy::ReadOnThread(0, 0, dst, src, sizeof(src), 30);
  EXPECT_EQ(ret, HCCL_E_AGAIN);
  EXPECT_GT(GetBasicTransferCallCount(), 1U);
}

// timeout_ms 为 0 时不重试，仅调用一次。
TEST_F(HcommProxyTransferRetryTest, ReadNoRetryWhenTimeoutZero) {
  SetBasicTransferAgainCount(1000000U);
  uint8_t src[8] = {1};
  uint8_t dst[8] = {0};

  int32_t ret = HcommProxy::ReadOnThread(0, 0, dst, src, sizeof(src), 0);
  EXPECT_EQ(ret, HCCL_E_AGAIN);
  EXPECT_EQ(GetBasicTransferCallCount(), 1U);
}

TEST_F(HcommProxyTransferRetryTest, WriteRetriesThenSucceeds) {
  constexpr uint32_t kAgainTimes = 2U;
  SetBasicTransferAgainCount(kAgainTimes);
  uint8_t src[8] = {9, 8, 7, 6, 5, 4, 3, 2};
  uint8_t dst[8] = {0};

  int32_t ret = HcommProxy::WriteOnThread(0, 0, dst, src, sizeof(src), 1000);
  EXPECT_EQ(ret, HCCL_SUCCESS);
  EXPECT_EQ(GetBasicTransferCallCount(), kAgainTimes + 1U);
  EXPECT_EQ(dst[0], src[0]);
}

TEST_F(HcommProxyTransferRetryTest, WriteStopsOnTimeout) {
  SetBasicTransferAgainCount(1000000U);
  uint8_t src[8] = {1};
  uint8_t dst[8] = {0};

  int32_t ret = HcommProxy::WriteOnThread(0, 0, dst, src, sizeof(src), 30);
  EXPECT_EQ(ret, HCCL_E_AGAIN);
  EXPECT_GT(GetBasicTransferCallCount(), 1U);
}

}  // namespace
}  // namespace hixl
