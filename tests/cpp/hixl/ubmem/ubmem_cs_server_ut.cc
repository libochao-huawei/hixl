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
#include <memory>

#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "cs/hixl_cs.h"
#include "cs/transfer_pool.h"
#include "depends/hccl/src/hccl_stub.h"
#include "engine/endpoint_test_utils.h"
#include "engine/test_mmpa_utils.h"
#include "hcomm/hcomm_res_defs.h"
#include "hixl/hixl_types.h"

extern "C" uint32_t GetThreadAllocCallCount();
extern "C" void ResetThreadLifecycleStats();

namespace hixl {
namespace {
constexpr int32_t kUbMemCsServerDevId = 910262;
constexpr uint32_t kUbMemCsServerPort = 26664U;
constexpr const char *kSingleSlotPoolConfig = R"({"comm_resource_config.max_active_channels":1})";

EndpointDesc MakeUbMemEp(EndpointLocType loc) {
  EndpointDesc ep{};
  ep.loc.locType = loc;
  ep.protocol = COMM_PROTOCOL_UB_MEM;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 1U;
  return ep;
}

HixlStatus CreateServer(EndpointLocType loc, HixlServerHandle *handle) {
  EndpointDesc ep = MakeUbMemEp(loc);
  HixlServerConfig config{};
  config.global_resource_config = kSingleSlotPoolConfig;
  HixlServerDesc desc{};
  desc.server_ip = "127.0.0.1";
  desc.server_port = kUbMemCsServerPort;
  desc.endpoint_list = &ep;
  desc.endpoint_list_num = 1U;
  return HixlCSServerCreate(&desc, &config, handle);
}
}  // namespace

class UbMemCsServerUt : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", kUbMemCsServerDevId, 0, 9, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    hixl_test::InstallSysApiHooks(std::make_shared<test::KernelJsonMmpaStub>());
    VirtualMemoryManager::GetInstance().Finalize();
    ResetThreadLifecycleStats();
  }

  void TearDown() override {
    if (server_handle_ != nullptr) {
      (void)HixlCSServerDestroy(server_handle_);
      server_handle_ = nullptr;
    }
    auto *pool = TransferPool::GetInstance(kUbMemCsServerDevId);
    if (pool != nullptr) {
      pool->Finalize();
    }
    VirtualMemoryManager::GetInstance().Finalize();
    hixl_test::ResetSysApiHooks();
    llm::AclRuntimeStub::Reset();
  }

  std::shared_ptr<endpoint_test::MockAclRuntimeStub> acl_stub_;
  HixlServerHandle server_handle_{nullptr};
};

TEST_F(UbMemCsServerUt, DeviceCreateSkipsTransferPool) {
  ASSERT_EQ(CreateServer(ENDPOINT_LOC_TYPE_DEVICE, &server_handle_), HIXL_SUCCESS);
  EXPECT_EQ(GetThreadAllocCallCount(), 0U);
  auto *pool = TransferPool::GetInstance(kUbMemCsServerDevId);
  ASSERT_NE(pool, nullptr);
  EXPECT_FALSE(pool->IsInitialized());
}

TEST_F(UbMemCsServerUt, HostCreateRejectsNonDeviceEndpoint) {
  EXPECT_EQ(CreateServer(ENDPOINT_LOC_TYPE_HOST, &server_handle_), HIXL_PARAM_INVALID);
  EXPECT_EQ(server_handle_, nullptr);
  EXPECT_EQ(GetThreadAllocCallCount(), 0U);
  auto *pool = TransferPool::GetInstance(kUbMemCsServerDevId);
  ASSERT_NE(pool, nullptr);
  EXPECT_FALSE(pool->IsInitialized());
}

TEST_F(UbMemCsServerUt, HostCreateRejectsNullEndpointListWithPositiveCount) {
  HixlServerConfig config{};
  config.global_resource_config = kSingleSlotPoolConfig;
  HixlServerDesc desc{};
  desc.server_ip = "127.0.0.1";
  desc.server_port = kUbMemCsServerPort;
  desc.endpoint_list = nullptr;
  desc.endpoint_list_num = 1U;
  EXPECT_EQ(HixlCSServerCreate(&desc, &config, &server_handle_), HIXL_PARAM_INVALID);
  EXPECT_EQ(server_handle_, nullptr);
}
}  // namespace hixl
