/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdlib>
#include <gtest/gtest.h>
#include "engine/hixl_engine_options.h"
#include "hixl/hixl_types.h"
#include "adxl/adxl_types.h"
#include "slog_stub.h"

namespace hixl {

class HixlEngineOptionsUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    unsetenv("HCCL_RDMA_TC");
    unsetenv("HCCL_RDMA_SL");
  }

  void TearDown() override {
    unsetenv("HCCL_RDMA_TC");
    unsetenv("HCCL_RDMA_SL");
  }
};

TEST_F(HixlEngineOptionsUTest, ParseEmptyOptions) {
  std::map<AscendString, AscendString> options;
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  EXPECT_FALSE(result.RdmaTrafficClass().has_value());
  EXPECT_FALSE(result.RdmaServiceLevel().has_value());
  EXPECT_FALSE(result.LocalCommRes().has_value());
  EXPECT_FALSE(result.EnableFabricMem().has_value());
  EXPECT_FALSE(result.AutoConnect().has_value());
  EXPECT_FALSE(result.GlobalResourceCfg().has_value());
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaTrafficClassHixlPrefix) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 132);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaTrafficClassAdxlPrefix) {
  std::map<AscendString, AscendString> options;
  options[adxl::OPTION_RDMA_TRAFFIC_CLASS] = "128";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 128);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaTrafficClassHixlPrefixTakesPrecedence) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[adxl::OPTION_RDMA_TRAFFIC_CLASS] = "128";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 132);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaTrafficClassFromEnv) {
  setenv("HCCL_RDMA_TC", "136", 1);
  std::map<AscendString, AscendString> options;
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 136);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaTrafficClassOptionOverridesEnv) {
  setenv("HCCL_RDMA_TC", "136", 1);
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaTrafficClass().has_value());
  EXPECT_EQ(*result.RdmaTrafficClass(), 132);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaTrafficClassInvalidValue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "256";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaTrafficClassNotMultipleOf4) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "130";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaServiceLevelHixlPrefix) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_SERVICE_LEVEL] = "4";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaServiceLevel().has_value());
  EXPECT_EQ(*result.RdmaServiceLevel(), 4);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaServiceLevelFromEnv) {
  setenv("HCCL_RDMA_SL", "7", 1);
  std::map<AscendString, AscendString> options;
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.RdmaServiceLevel().has_value());
  EXPECT_EQ(*result.RdmaServiceLevel(), 7);
}

TEST_F(HixlEngineOptionsUTest, ParseRdmaServiceLevelInvalidValue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_SERVICE_LEVEL] = "8";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseLocalCommResHixlPrefix) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_LOCAL_COMM_RES] = R"({"version":"1.3"})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), R"({"version":"1.3"})");
}

TEST_F(HixlEngineOptionsUTest, ParseLocalCommResAdxlPrefix) {
  std::map<AscendString, AscendString> options;
  options[adxl::OPTION_LOCAL_COMM_RES] = R"({"version":"1.2"})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.LocalCommRes().has_value());
  EXPECT_EQ(*result.LocalCommRes(), R"({"version":"1.2"})");
}

TEST_F(HixlEngineOptionsUTest, ParseBufferPoolValid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_BUFFER_POOL] = "0:0";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
}

TEST_F(HixlEngineOptionsUTest, ParseBufferPoolInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_BUFFER_POOL] = "1:0";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseEnableFabricMemTrue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.EnableFabricMem().has_value());
  EXPECT_TRUE(*result.EnableFabricMem());
}

TEST_F(HixlEngineOptionsUTest, ParseEnableFabricMemFalse) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "0";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.EnableFabricMem().has_value());
  EXPECT_FALSE(*result.EnableFabricMem());
}

TEST_F(HixlEngineOptionsUTest, ParseEnableFabricMemInvalid) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "2";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseAutoConnectTrue) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_AUTO_CONNECT] = "1";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.AutoConnect().has_value());
  EXPECT_TRUE(*result.AutoConnect());
}

TEST_F(HixlEngineOptionsUTest, ParseAutoConnectEmpty) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_AUTO_CONNECT] = "";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseGlobalResourceConfigFabricMemory) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"max_capacity":"10","start_address":"50","task_stream_num":"4"}})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.fabric_memory.max_capacity.has_value());
  EXPECT_EQ(*grc.fabric_memory.max_capacity, 10U);
  ASSERT_TRUE(grc.fabric_memory.start_address.has_value());
  EXPECT_EQ(*grc.fabric_memory.start_address, 50U);
  ASSERT_TRUE(grc.fabric_memory.task_stream_num.has_value());
  EXPECT_EQ(*grc.fabric_memory.task_stream_num, 4U);
}

TEST_F(HixlEngineOptionsUTest, ParseGlobalResourceConfigConnectPool) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"connect_pool.thread_num":"4","connect_pool.task_queue_capacity":"256"})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.connect_pool.thread_num.has_value());
  EXPECT_EQ(*grc.connect_pool.thread_num, 4);
  ASSERT_TRUE(grc.connect_pool.task_queue_capacity.has_value());
  EXPECT_EQ(*grc.connect_pool.task_queue_capacity, 256);
}

TEST_F(HixlEngineOptionsUTest, ParseGlobalResourceConfigProtocolDesc) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"comm_resource_config.protocol_desc":["uboe:device"]})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  ASSERT_TRUE(result.GlobalResourceCfg().has_value());
  auto grc = *result.GlobalResourceCfg();
  ASSERT_TRUE(grc.comm_resource_config.protocol_desc.has_value());
  EXPECT_EQ(grc.comm_resource_config.protocol_desc->size(), 1U);
  EXPECT_EQ((*grc.comm_resource_config.protocol_desc)[0], "uboe:device");
}

TEST_F(HixlEngineOptionsUTest, ParseGlobalResourceConfigInvalidJson) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = "not json";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseGlobalResourceConfigNotObject) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = "[]";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseFabricMemCapacityOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"max_capacity":"2048"}})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseFabricMemStartAddressOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"start_address":"250"}})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, ParseFabricMemTaskStreamNumOutOfRange) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
      R"({"fabric_memory":{"task_stream_num":"16"}})";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, CheckSupportedOptionsAllSupported) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[hixl::OPTION_LOCAL_COMM_RES] = "{}";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  std::unordered_set<std::string> whitelist = {
      hixl::OPTION_RDMA_TRAFFIC_CLASS, hixl::OPTION_LOCAL_COMM_RES};
  EXPECT_EQ(result.CheckSupportedOptions(whitelist), SUCCESS);
}

TEST_F(HixlEngineOptionsUTest, CheckSupportedOptionsUnsupported) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  std::unordered_set<std::string> whitelist = {hixl::OPTION_RDMA_TRAFFIC_CLASS};
  EXPECT_EQ(result.CheckSupportedOptions(whitelist), PARAM_INVALID);
}

TEST_F(HixlEngineOptionsUTest, CheckSupportedOptionsEmpty) {
  std::map<AscendString, AscendString> options;
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  std::unordered_set<std::string> whitelist = {hixl::OPTION_RDMA_TRAFFIC_CLASS};
  EXPECT_EQ(result.CheckSupportedOptions(whitelist), SUCCESS);
}

TEST_F(HixlEngineOptionsUTest, RawOptionsPreserved) {
  std::map<AscendString, AscendString> options;
  options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
  options[hixl::OPTION_LOCAL_COMM_RES] = "test_value";
  HixlEngineOptions result;
  EXPECT_EQ(HixlEngineOptions::Parse(options, result), SUCCESS);
  EXPECT_EQ(result.RawOptions().size(), 2U);
  EXPECT_EQ(result.RawOptions().at(hixl::OPTION_RDMA_TRAFFIC_CLASS).GetString(), std::string("132"));
}

}  // namespace hixl
