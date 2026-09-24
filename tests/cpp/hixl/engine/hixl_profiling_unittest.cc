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
#include <type_traits>

#include "acl/acl_base.h"
#include "aprof_pub.h"
#include "ascendcl_stub.h"
#include "profiling/prof_reporter.h"
#include "profiling/prof_proxy.h"
#include "depends/msprof/src/msprof_stub.h"

#define kAclProfHixlApi 0x0001U
#define kStartProfiling 1U
#define kStopProfiling 2U

namespace hixl {
class HixlProfilingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    llm::SetAclProfCreateStampEnabled(true);
  }
  void TearDown() override {
    llm::SetAclProfCreateStampEnabled(false);
  }
};

TEST_F(HixlProfilingTest, AclProfVersionSelectionHandlesBoundaryAndFailures) {
  const auto query_version = [](char *, int32_t *version_num) -> int32_t {
    *version_num = 90200000;
    return 0;
  };
  const auto query_new_version = [](char *, int32_t *version_num) -> int32_t {
    *version_num = 90200701;
    return 0;
  };
  const auto query_failure = [](char *, int32_t *) -> int32_t { return -1; };
  auto dummy_create = []() -> void * { return reinterpret_cast<void *>(1); };

  EXPECT_EQ(kAclProfRuntimeVersionThreshold, 90200000);
  EXPECT_FALSE(HixlIsAclProfRuntimeSupported(nullptr));
  EXPECT_FALSE(HixlIsAclProfRuntimeSupported(query_version));
  EXPECT_TRUE(HixlIsAclProfRuntimeSupported(query_new_version));
  EXPECT_FALSE(HixlIsAclProfRuntimeSupported(query_failure));
  EXPECT_EQ(HixlResolveProfFuncs(dummy_create, nullptr).start, MsprofStartRange);
  EXPECT_EQ(HixlResolveProfFuncs(dummy_create, query_version).start, MsprofStartRange);
  EXPECT_EQ(HixlResolveProfFuncs(nullptr, query_new_version).start, MsprofStartRange);
  EXPECT_EQ(HixlResolveProfFuncs(dummy_create, query_failure).start, MsprofStartRange);
  EXPECT_EQ(HixlResolveProfFuncs(dummy_create, query_new_version).start, AclStartRange);
}

TEST_F(HixlProfilingTest, ReporterRangeLifecycle) {
  const uint64_t stop_count = llm::GetAclProfRangeStopCount();
  const uint64_t destroy_count = llm::GetAclProfStampDestroyCount();
  { HixlProfilingReporter reporter(HixlProfType::HixlOpBatchRead); }
  EXPECT_EQ(llm::GetAclProfRangeStopCount(), stop_count + 1U);
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count + 1U);
}

TEST_F(HixlProfilingTest, ProfilingGuardsAreNonCopyable) {
  static_assert(!std::is_copy_constructible<HixlProfilingReporter>::value,
                "HixlProfilingReporter must not be copy constructible");
  static_assert(!std::is_copy_assignable<HixlProfilingReporter>::value,
                "HixlProfilingReporter must not be copy assignable");
  static_assert(!std::is_copy_constructible<ProfStart>::value, "ProfStart must not be copy constructible");
  static_assert(!std::is_copy_assignable<ProfStart>::value, "ProfStart must not be copy assignable");
  SUCCEED();
}

TEST_F(HixlProfilingTest, AsyncRangeLifecycle) {
  const uint64_t stop_count = llm::GetAclProfRangeStopCount();
  const uint64_t destroy_count = llm::GetAclProfStampDestroyCount();
  { auto prof_start = GetProfStart(HixlProfType::HixlOpBatchWrite); }
  EXPECT_EQ(llm::GetAclProfRangeStopCount(), stop_count);
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count + 1U);
}

TEST_F(HixlProfilingTest, AsyncRangeExplicitStopIsIdempotent) {
  const uint64_t stop_count = llm::GetAclProfRangeStopCount();
  const uint64_t destroy_count = llm::GetAclProfStampDestroyCount();
  auto prof_start = GetProfStart(HixlProfType::HixlOpBatchWrite);
  prof_start->Stop();
  prof_start->Stop();
  EXPECT_EQ(llm::GetAclProfRangeStopCount(), stop_count + 1U);
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count + 1U);
  prof_start.reset();
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count + 1U);
}

TEST_F(HixlProfilingTest, AsyncRangeMacroStopsAndDestroysRange) {
  const uint64_t stop_count = llm::GetAclProfRangeStopCount();
  const uint64_t destroy_count = llm::GetAclProfStampDestroyCount();
  auto prof_start = GetProfStart(HixlProfType::HixlOpBatchWrite);
  { HIXL_API_PROFILING_WITH_PROF_START(prof_start); }
  EXPECT_EQ(llm::GetAclProfRangeStopCount(), stop_count + 1U);
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count + 1U);
  prof_start.reset();
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count + 1U);
}

TEST_F(HixlProfilingTest, CreateStampDisabledDoesNotAllocateRange) {
  llm::SetAclProfCreateStampEnabled(false);
  const uint64_t create_count = llm::GetAclProfStampCreateCount();
  const uint64_t stop_count = llm::GetAclProfRangeStopCount();
  const uint64_t destroy_count = llm::GetAclProfStampDestroyCount();
  {
    HixlProfilingReporter reporter(HixlProfType::HixlOpBatchRead);
    auto prof_start = GetProfStart(HixlProfType::HixlOpBatchWrite);
    { HIXL_API_PROFILING_WITH_PROF_START(prof_start); }
  }
  EXPECT_EQ(llm::GetAclProfStampCreateCount(), create_count);
  EXPECT_EQ(llm::GetAclProfRangeStopCount(), stop_count);
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count);
}

TEST_F(HixlProfilingTest, AutoRegisterCallback) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
}

TEST_F(HixlProfilingTest, StartProfilingOK) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;

  int32_t ret = cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
  EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HixlProfilingTest, StopProfilingOK) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStopProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;

  int32_t ret = cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
  EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HixlProfilingTest, StartProfilingRegisterTypeFails) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
  MsprofCommandHandle stop_cfg{};
  stop_cfg.profSwitch = kAclProfHixlApi;
  stop_cfg.type = kStopProfiling;
  stop_cfg.devIdList[0] = 0;
  stop_cfg.devNums = 1;
  (void)cb(PROF_CTRL_SWITCH, &stop_cfg, sizeof(stop_cfg));

  SetMsprofRegTypeInfoRet(1);
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;
  int32_t ret = cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg));
  EXPECT_EQ(ret, ACL_ERROR_PROFILING_FAILURE);
  SetMsprofRegTypeInfoRet(0);
}

TEST_F(HixlProfilingTest, DataNullReturnErr) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
  int32_t ret = cb(PROF_CTRL_SWITCH, nullptr, 100);
  EXPECT_EQ(ret, ACL_ERROR_INVALID_PARAM);
}

TEST_F(HixlProfilingTest, DataLenTooSmallReturnErr) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
  int dummy = 0;
  int32_t ret = cb(PROF_CTRL_SWITCH, &dummy, 4);
  EXPECT_EQ(ret, ACL_ERROR_INVALID_PARAM);
}

TEST_F(HixlProfilingTest, InvalidTypeReturnOK) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;
  int32_t ret = cb(0xFFFF, &cfg, 0);
  EXPECT_EQ(ret, ACL_SUCCESS);
}

TEST_F(HixlProfilingTest, ReporterLifecycleAfterProfilingStart) {
  ProfCommandHandle cb = GetHixlProfCallback();
  ASSERT_NE(cb, nullptr);
  MsprofCommandHandle cfg{};
  cfg.profSwitch = kAclProfHixlApi;
  cfg.type = kStartProfiling;
  cfg.devIdList[0] = 0;
  cfg.devNums = 1;
  ASSERT_EQ(cb(PROF_CTRL_SWITCH, &cfg, sizeof(cfg)), ACL_SUCCESS);

  const uint64_t stop_count = llm::GetAclProfRangeStopCount();
  const uint64_t destroy_count = llm::GetAclProfStampDestroyCount();
  { HixlProfilingReporter reporter(HixlProfType::HixlOpBatchRead); }
  EXPECT_EQ(llm::GetAclProfRangeStopCount(), stop_count + 1U);
  EXPECT_EQ(llm::GetAclProfStampDestroyCount(), destroy_count + 1U);
}
}  // namespace hixl
