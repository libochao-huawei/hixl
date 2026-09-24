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
#include <limits>
#include <sys/socket.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "common/ctrl_msg_plugin.h"
#include "common/scope_guard.h"

namespace hixl {
TEST(CtrlMsgPluginTest, RejectsLengthThatCannotBeRepresentedBySsizeT) {
  if (std::numeric_limits<size_t>::digits <= std::numeric_limits<ssize_t>::digits) {
    GTEST_SKIP() << "size_t does not provide a value outside the ssize_t range";
  }

  const size_t first_invalid_length = static_cast<size_t>(std::numeric_limits<ssize_t>::max()) + 1U;
  const std::array<size_t, 2> invalid_lengths = {first_invalid_length, std::numeric_limits<size_t>::max()};
  char byte = 0;
  for (const size_t len : invalid_lengths) {
    int32_t err_no = -1;
    EXPECT_EQ(CtrlMsgPlugin::Send(-1, &byte, len, err_no), PARAM_INVALID) << "len=" << len;
    EXPECT_EQ(CtrlMsgPlugin::Recv(-1, &byte, len, 0), PARAM_INVALID) << "len=" << len;
  }
}

TEST(CtrlMsgPluginTest, AcceptsRepresentableBoundaryLengths) {
  if (std::numeric_limits<size_t>::digits < std::numeric_limits<ssize_t>::digits) {
    GTEST_SKIP() << "size_t cannot represent ssize_t max";
  }

  const size_t max_length = static_cast<size_t>(std::numeric_limits<ssize_t>::max());
  char byte = 0;
  int32_t err_no = -1;
  EXPECT_EQ(CtrlMsgPlugin::Send(-1, &byte, 0, err_no), SUCCESS);
  EXPECT_EQ(CtrlMsgPlugin::Send(-1, &byte, max_length, err_no), FAILED);
  EXPECT_EQ(CtrlMsgPlugin::Recv(-1, &byte, 0, 0), SUCCESS);
  EXPECT_EQ(CtrlMsgPlugin::Recv(-1, &byte, max_length, 0), TIMEOUT);
}

TEST(CtrlMsgPluginTest, SendsSmallBuffer) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  HIXL_MAKE_GUARD(close_fds, ([&fds]() {
                    (void)close(fds[0]);
                    (void)close(fds[1]);
                  }));

  const std::array<char, 7> expected = {'h', 'i', 'x', 'l', '-', 'u', 't'};
  ASSERT_EQ(CtrlMsgPlugin::Send(fds[0], expected.data(), expected.size()), SUCCESS);

  std::array<char, 7> actual = {};
  ASSERT_EQ(read(fds[1], actual.data(), actual.size()), static_cast<ssize_t>(actual.size()));
  EXPECT_EQ(actual, expected);
}
}  // namespace hixl
