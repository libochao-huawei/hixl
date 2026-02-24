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
#include "gtest/gtest.h"
#include "hixl/hixl_types.h"
#include "hixl_kernel/hixl_batch_transfer.h"

namespace {
template <size_t N>
HixlOneSideOpParam CreateTestParamFixed(std::array<std::array<uint8_t, 8>, N> &srcBuffers,
                                        std::array<std::array<uint8_t, 8>, N> &dstBuffers,
                                        std::array<uint64_t, N> &lensStorage, ThreadHandle thread = 0ULL,
                                        ChannelHandle channel = 0ULL, uint64_t remoteFlag = 1ULL,
                                        uint64_t localFlag = 0ULL) {
  // 在调用者栈上创建数组，通过引用传递
  static std::array<void *, N> srcPtrs;
  static std::array<void *, N> dstPtrs;
  static std::array<uint64_t, N> lens;

  for (size_t i = 0; i < N; ++i) {
    srcPtrs[i] = srcBuffers[i].data();
    dstPtrs[i] = dstBuffers[i].data();
    lens[i] = lensStorage[i];
  }

  HixlOneSideOpParam param{};
  param.thread = thread;
  param.channel = channel;
  param.list_num = static_cast<uint32_t>(N);
  param.dst_buf_list = dstPtrs.data();
  param.src_buf_list = srcPtrs.data();
  param.len_list = lens.data();
  param.remote_flag = remoteFlag;
  param.local_flag = localFlag;
  param.flag_size = sizeof(uint64_t);

  return param;
}
}  // namespace

using namespace hixl;
TEST(HixlKernelBasicTest, BatchPutSuccess) {
  // 模拟本地源数据和远端目标缓冲区
  std::array<std::array<uint8_t, 8>, 3> localSrc{};
  std::array<std::array<uint8_t, 8>, 3> remoteDst{};
  std::array<uint64_t, 3> lensStorage{8, 8, 8};
  for (auto &arr : localSrc) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : remoteDst) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }
  auto param = CreateTestParamFixed<3>(localSrc, remoteDst, lensStorage);
  uint32_t ret = HixlBatchPut(&param);
  EXPECT_EQ(ret, SUCCESS);
}

TEST(HixlKernelBasicTest, BatchGetSuccess) {
  // 模拟远端源数据和本地目标缓冲区
  std::array<std::array<uint8_t, 8>, 3> remoteSrc{};
  std::array<std::array<uint8_t, 8>, 3> localDst{};
  std::array<uint64_t, 3> lensStorage{8, 8, 8};
  for (auto &arr : remoteSrc) {
    std::fill(arr.begin(), arr.end(), 0xAA);
  }
  for (auto &arr : localDst) {
    std::fill(arr.begin(), arr.end(), 0xBB);
  }
  auto param = CreateTestParamFixed<3>(remoteSrc, localDst, lensStorage);
  uint32_t ret = HixlBatchGet(&param);
  EXPECT_EQ(ret, SUCCESS);
}