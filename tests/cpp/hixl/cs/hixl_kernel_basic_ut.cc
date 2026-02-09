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
#include "hixl_kernel/kernel_launch.h"

using namespace hixl;
TEST(HixlKernelBasicTest, BatchPutSuccess) {
  // 模拟本地源数据和远端目标缓冲区
  std::array<std::array<uint8_t, 8>, 3> localSrc{};
  std::array<std::array<uint8_t, 8>, 3> remoteDst{};

  std::array<const void*, 3> srcPtrs{ localSrc[0].data(), localSrc[1].data(), localSrc[2].data() };
  std::array<void*, 3> dstPtrs{ remoteDst[0].data(), remoteDst[1].data(), remoteDst[2].data() };
  std::array<uint64_t, 3> lens{ 8, 8, 8 };

  uint8_t remoteFlag[8] = {};
  uint8_t localFlag[8] = {};
  ThreadHandle thread = 0ULL;
  ChannelHandle channel = 0ULL;
  HixlOneSideOpParam p{};
  p.thread = thread;
  p.channel = channel;
  p.list_num = static_cast<uint32_t>(dstPtrs.size());
  p.dst_buf_list = dstPtrs.data();
  p.src_buf_list = srcPtrs.data();
  p.len_list = lens.data();
  p.remote_flag = remoteFlag;
  p.local_flag = localFlag;
  p.flag_size = 8;

  unsigned int ret = HixlBatchPut(&p);
  EXPECT_EQ(ret, SUCCESS);
}

TEST(HixlKernelBasicTest, BatchGetSuccess) {
  // 模拟远端源数据和本地目标缓冲区
  std::array<std::array<uint8_t, 8>, 3> remoteSrc{};
  std::array<std::array<uint8_t, 8>, 3> localDst{};

  std::array<const void*, 3> srcPtrs{ remoteSrc[0].data(), remoteSrc[1].data(), remoteSrc[2].data() };
  std::array<void*, 3> dstPtrs{ localDst[0].data(), localDst[1].data(), localDst[2].data() };
  std::array<uint64_t, 3> lens{ 8, 8, 8 };

  uint8_t remoteFlag[8] = {};
  uint8_t localFlag[8] = {};
  ThreadHandle thread = 0ULL;
  ChannelHandle channel = 0ULL;
  HixlOneSideOpParam p{};
  p.thread = thread;
  p.channel = channel;
  p.list_num = static_cast<uint32_t>(dstPtrs.size());
  p.dst_buf_list = dstPtrs.data();
  p.src_buf_list = srcPtrs.data();
  p.len_list = lens.data();
  p.remote_flag = remoteFlag;
  p.local_flag = localFlag;
  p.flag_size = 8;

  unsigned int ret = HixlBatchGet(&p);
  EXPECT_EQ(ret, SUCCESS);
}