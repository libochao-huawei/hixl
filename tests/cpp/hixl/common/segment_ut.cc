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
#include "common/segment.h"

namespace hixl {
namespace {

TEST(SegmentTest, ContainsDoesNotBridgeOneByteGap) {
  Segment segment(MEM_DEVICE);
  ASSERT_EQ(segment.AddRange(0x1000U, 0xAU), SUCCESS);
  ASSERT_EQ(segment.AddRange(0x100BU, 0x9U), SUCCESS);

  EXPECT_FALSE(segment.Contains(0x1000U, 0x1014U));
  EXPECT_FALSE(segment.Contains(0x1009U, 0x100CU));
  EXPECT_TRUE(segment.Contains(0x1000U, 0x100AU));
  EXPECT_TRUE(segment.Contains(0x100BU, 0x1014U));
}

TEST(SegmentTest, ContainsBridgesAdjacentHalfOpenRanges) {
  Segment segment(MEM_DEVICE);
  ASSERT_EQ(segment.AddRange(0x1000U, 0xAU), SUCCESS);
  ASSERT_EQ(segment.AddRange(0x100AU, 0xAU), SUCCESS);

  EXPECT_TRUE(segment.Contains(0x1000U, 0x1014U));
}

TEST(SegmentTest, ContainsStopsAfterConnectedRangesAtGap) {
  Segment segment(MEM_DEVICE);
  ASSERT_EQ(segment.AddRange(0x1000U, 0xAU), SUCCESS);
  ASSERT_EQ(segment.AddRange(0x100AU, 0xAU), SUCCESS);
  ASSERT_EQ(segment.AddRange(0x1015U, 0xAU), SUCCESS);

  EXPECT_TRUE(segment.Contains(0x1000U, 0x1014U));
  EXPECT_FALSE(segment.Contains(0x1000U, 0x101FU));
}

}  // namespace
}  // namespace hixl
