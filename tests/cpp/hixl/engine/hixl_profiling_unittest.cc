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
#include "hixl/hixl_types.h"
#include "common/prof_api_reg.h"
#include "profiling/aprof_pub.h"

extern "C" {
    typedef int32_t (*ProfCommandHandle)(uint32_t, void*, uint32_t);
    extern ProfCommandHandle g_hixl_prof_callback;
}

#define PROF_CTRL_SWITCH      1
#define ACL_PROF_HIXL_API     0x0001U
#define START_PROFILING       1U
#define STOP_PROFILING        2U

namespace hixl {
class HixlProfilingTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// 测试1：全局回调已自动注册
TEST(HixlProfilingTest, AutoRegisterCallback) {
  ASSERT_NE(g_hixl_prof_callback, nullptr);
}

// 测试2：正常Start → 返回0
TEST(HixlProfilingTest, StartProfilingOK) {
  MsprofCommandHandle cfg{};
  cfg.profSwitch = ACL_PROF_HIXL_API;
  cfg.type = START_PROFILING;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;

  int32_t ret = g_hixl_prof_callback(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
  EXPECT_EQ(ret, 0); // 成功
}

// 测试3：正常Stop → 返回0
TEST(HixlProfilingTest, StopProfilingOK) {
  MsprofCommandHandle cfg{};
  cfg.profSwitch = ACL_PROF_HIXL_API;
  cfg.type = STOP_PROFILING;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;

  int32_t ret = g_hixl_prof_callback(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
  EXPECT_EQ(ret, 0);
}

// 测试6：数据指针为空 → 返回-1
TEST(HixlProfilingTest, DataNullReturnErr) {
  int32_t ret = g_hixl_prof_callback(PROF_CTRL_SWITCH, nullptr, 100);
  EXPECT_EQ(ret, -1);
}

// 测试7：数据长度太短 → 返回-1
TEST(HixlProfilingTest, DataLenTooSmallReturnErr) {
  int dummy = 0;
  int32_t ret = g_hixl_prof_callback(PROF_CTRL_SWITCH, &dummy, 4);
  EXPECT_EQ(ret, -1);
}

// 测试8：不支持的data_type → 返回-1
TEST(HixlProfilingTest, InvalidTypeReturnOK) {
  int32_t ret = g_hixl_prof_callback(0xFFFF, nullptr, 0);
  EXPECT_EQ(ret, -1);
}

// 测试9：Reporter开启路径（prof_run=true）
TEST(HixlProfilingTest, ReporterEnableRun) {
  MsprofCommandHandle cfg{};
  cfg.profSwitch = ACL_PROF_HIXL_API;
  cfg.type = START_PROFILING;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;
  g_hixl_prof_callback(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));

  HixlProfilingReporter reporter(HixlProfType::HixlProfTypeRead);
  // 无崩溃 = 覆盖成功
}

// 测试10：Reporter关闭路径（prof_run=false）
TEST(HixlProfilingTest, ReporterDisableNoRun) {
  HixlProfilingReporter reporter(HixlProfType::HixlProfTypeWrite);
  // 无崩溃 = 覆盖成功
}
}