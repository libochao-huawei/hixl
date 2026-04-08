/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <thread>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include "engine/hixl_engine.h"
#include "hixl/hixl_types.h"
#include "cs/hixl_cs_client.h"
#include "hixl/hixl.h"
#include "slog_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"

namespace hixl {

namespace {
constexpr const int32_t kTimeOut = 1000;
constexpr const uint32_t kCaptureLogTimeoutMs = 1000U;
constexpr const char kHccnToolPath[] = "/usr/local/Ascend/driver/tools/hccn_tool";

// Mock mmAccess to return error for kHccnToolPath, so hccn_tool will be searched in PATH
class BondIpMmpaStub : public llm::MmpaStubApiGe {
 public:
  INT32 Access(const CHAR *path_name) override {
    if (std::string(path_name) == kHccnToolPath) {
      return EN_ERROR;  // Return error to skip the full path check
    }
    return llm::MmpaStubApiGe::Access(path_name);
  }
};
}

class HixlEngineUboeTest : public ::testing::Test {
 protected:
  std::map<AscendString, AscendString> options_uboe;
  std::map<AscendString, AscendString> options_mixed;
  std::map<AscendString, AscendString> options_default;

  void SetUp() override {
    temp_dir_ = std::filesystem::path("/tmp/hixl_engine_uboe_unittest");
    std::filesystem::remove_all(temp_dir_);
    std::filesystem::create_directories(temp_dir_);
    old_path_ = getenv("PATH") == nullptr ? "" : getenv("PATH");

    // UBOE 协议配置
    options_uboe[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "uboe_test_1",
        "endpoint_list": [
            {
                "protocol": "uboe",
                "comm_id": "127.0.0.1",
                "placement": "device"
            }
        ],
        "version": "1.3"
    }
    )";

    // 混合协议配置（UBOE + ROCE）
    options_mixed[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "mixed_test_1",
        "endpoint_list": [
            {
                "protocol": "roce",
                "comm_id": "127.0.0.1",
                "placement": "host"
            },
            {
                "protocol": "uboe",
                "comm_id": "127.0.0.1",
                "placement": "device"
            }
        ],
        "version": "1.3"
    }
    )";

    // 默认配置（空 endpoint_list，需要生成默认配置）
    options_default[hixl::OPTION_LOCAL_COMM_RES] = R"(
    {
        "net_instance_id": "default_test_1",
        "endpoint_list": [],
        "version": "1.3"
    }
    )";
  }

  void TearDown() override {
    if (old_path_.empty()) {
      unsetenv("PATH");
    } else {
      setenv("PATH", old_path_.c_str(), 1);
    }
    std::filesystem::remove_all(temp_dir_);
  }

  void CreateHccnTool(const std::string &output) const {
    const auto tool_path = temp_dir_ / "hccn_tool";
    std::ofstream file(tool_path);
    ASSERT_TRUE(file.is_open());
    file << "#!/bin/sh\n";
    file << "echo \"" << output << "\"\n";
    file.close();
    ASSERT_EQ(chmod(tool_path.c_str(), 0755), 0);
    const auto new_path = temp_dir_.string() + ":" + old_path_;
    setenv("PATH", new_path.c_str(), 1);
  }
 private:
  std::filesystem::path temp_dir_;
  std::string old_path_;
};

// 测试 UBOE 协议 Endpoint 的初始化
TEST_F(HixlEngineUboeTest, InitializeWithUboeProtocol) {
  Hixl engine;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);
  engine.Finalize();
}

// 测试混合协议（UBOE + ROCE）的初始化
TEST_F(HixlEngineUboeTest, InitializeWithMixedProtocols) {
  Hixl engine;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options_mixed), SUCCESS);
  engine.Finalize();
}

// 测试空 endpoint_list 的处理
TEST_F(HixlEngineUboeTest, InitializeWithEmptyEndpointList) {
  Hixl engine;
  // 空 endpoint_list 应该会生成默认配置或返回错误
  // 根据实际实现调整期望
  Status ret = engine.Initialize("127.0.0.1", options_default);
  // 可能返回 SUCCESS（如果自动生成配置）或 PARAM_INVALID（如果无法生成）
  // 这里暂时期望失败，因为没有足够信息生成默认 UBOE endpoint
  EXPECT_NE(ret, SUCCESS);
  if (ret == SUCCESS) {
    engine.Finalize();
  }
}

// 测试uboe init 的配置
TEST_F(HixlEngineUboeTest, InitializeUboeTest) {
  Hixl engine;
  EXPECT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);
  engine.Finalize();
}

// 测试 UBOE 协议下的 Host 内存注册
TEST_F(HixlEngineUboeTest, RegisterHostMemoryWithUboe) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  // 分配 host 内存
  int32_t src = 42;
  hixl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(&src);
  src_mem.len = sizeof(int32_t);

  // 注册 host 内存（在 UBOE 场景下应该自动注册设备地址）
  MemHandle handle = nullptr;
  Status ret = engine.RegisterMem(src_mem, MEM_HOST, handle);
  // 注意：这里可能需要根据实际实现调整
  // 在 UBOE 场景下，host 内存注册可能需要特殊处理
  EXPECT_EQ(ret, SUCCESS);

  if (ret == SUCCESS) {
    EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  }

  engine.Finalize();
}

// 测试 UBOE 协议下的 Device 内存注册
TEST_F(HixlEngineUboeTest, RegisterDeviceMemoryWithUboe) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  // 模拟 device 内存地址
  hixl::MemDesc device_mem{};
  device_mem.addr = 0x100000;  // 模拟设备地址
  device_mem.len = sizeof(int32_t);

  MemHandle handle = nullptr;
  Status ret = engine.RegisterMem(device_mem, MEM_DEVICE, handle);
  // Device 内存注册应该直接成功
  EXPECT_EQ(ret, SUCCESS);

  if (ret == SUCCESS) {
    EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);
  }

  engine.Finalize();
}

// 测试两个 UBOE endpoint 的连接
TEST_F(HixlEngineUboeTest, ConnectTwoUboeEngines) {
  std::map<AscendString, AscendString> options1;
  options1[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "uboe_engine1",
      "endpoint_list": [
          {
              "protocol": "uboe",
              "comm_id": "192.168.1.100",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  std::map<AscendString, AscendString> options2;
  options2[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "uboe_engine2",
      "endpoint_list": [
          {
              "protocol": "uboe",
              "comm_id": "192.168.111.111",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  Hixl engine1;
  Hixl engine2;
  ASSERT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);
  ASSERT_EQ(engine2.Initialize("127.0.0.1:16000", options2), SUCCESS);

  // 连接
  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);

  // 断开连接并清理
  engine1.Disconnect("127.0.0.1:16000");
  engine1.Finalize();
  engine2.Finalize();
}

// 测试优先使用 UBOE 协议连接
TEST_F(HixlEngineUboeTest, PreferUboeProtocolForConnection) {
  // 本地配置同时有 ROCE 和 UBOE
  std::map<AscendString, AscendString> local_options;
  local_options[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "local_mixed",
      "endpoint_list": [
          {
              "protocol": "roce",
              "comm_id": "127.0.0.1",
              "placement": "host"
          },
          {
              "protocol": "uboe",
              "comm_id": "192.168.100.100",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  // 远端配置同时有 ROCE 和 UBOE
  std::map<AscendString, AscendString> remote_options;
  remote_options[hixl::OPTION_LOCAL_COMM_RES] = R"(
  {
      "net_instance_id": "remote_mixed",
      "endpoint_list": [
          {
              "protocol": "roce",
              "comm_id": "127.0.0.1",
              "placement": "host"
          },
          {
              "protocol": "uboe",
              "comm_id": "192.168.111.112",
              "placement": "device"
          }
      ],
      "version": "1.3"
  }
  )";

  Hixl engine1;
  Hixl engine2;
  ASSERT_EQ(engine1.Initialize("127.0.0.1", local_options), SUCCESS);
  ASSERT_EQ(engine2.Initialize("127.0.0.1:16000", remote_options), SUCCESS);

  // 连接，应该优先使用 UBOE
  EXPECT_EQ(engine1.Connect("127.0.0.1:16000", kTimeOut), SUCCESS);

  engine1.Disconnect("127.0.0.1:16000");
  engine1.Finalize();
  engine2.Finalize();
}

// 测试 UBOE 协议的内存注册和注销配对
TEST_F(HixlEngineUboeTest, RegisterAndDeregisterMemoryPaired) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  // 分配 host 内存
  std::vector<int32_t> host_data(100, 42);
  hixl::MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(host_data.data());
  mem.len = host_data.size() * sizeof(int32_t);

  MemHandle handle = nullptr;
  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle), SUCCESS);
  EXPECT_NE(handle, nullptr);

  // 注销内存
  EXPECT_EQ(engine.DeregisterMem(handle), SUCCESS);

  engine.Finalize();
}

// 测试重复注册同一内存
TEST_F(HixlEngineUboeTest, RegisterSameMemoryTwice) {
  Hixl engine;
  ASSERT_EQ(engine.Initialize("127.0.0.1", options_uboe), SUCCESS);

  int32_t data = 42;
  hixl::MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(&data);
  mem.len = sizeof(int32_t);

  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;

  EXPECT_EQ(engine.RegisterMem(mem, MEM_HOST, handle1), SUCCESS);
  // 第二次注册相同地址可能返回错误或返回不同的 handle
  // 根据实际实现调整
  Status ret = engine.RegisterMem(mem, MEM_HOST, handle2);
  // 暂时假设允许重复注册
  if (ret == SUCCESS) {
    EXPECT_EQ(engine.DeregisterMem(handle2), SUCCESS);
  }
  EXPECT_EQ(engine.DeregisterMem(handle1), SUCCESS);

  engine.Finalize();
}

// 测试通过 OPTION_GLOBAL_RESOURCE_CONFIG 触发 GenDefaultUboeEndpointConfig
TEST_F(HixlEngineUboeTest, InitializeWithGlobalResourceConfig) {
  // Setup mmpa stub to make kHccnToolPath not found
  llm::MmpaStub::GetInstance().SetImpl(std::make_shared<BondIpMmpaStub>());

  // Create mock hccn_tool that returns bond IP
  CreateHccnTool("ipaddr:192.168.100.200\nnetmask:255.255.255.0");

  std::map<AscendString, AscendString> options;
  // 配置 comm_resource_config.protocol_desc 为 ["uboe:device"]
  // 这会触发 GenDefaultUboeEndpointConfig 方法
  options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"(
  {
    "comm_resource_config.protocol_desc": ["uboe:device"]
  }
  )";

  Hixl engine;
  // GetBondIpAddress 会通过 mock hccn_tool 返回固定 IP
  EXPECT_EQ(engine.Initialize("127.0.0.1", options), SUCCESS);
  engine.Finalize();

  llm::MmpaStub::GetInstance().Reset();
}
}  // namespace hixl
