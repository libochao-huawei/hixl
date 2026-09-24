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

#include "cs/ubmem/ubmem_endpoint.h"
#include "cs/ubmem/ubmem_channel.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "cs/hcomm_endpoint.h"
#include "cs/transfer_pool.h"
#include "depends/hccl/src/hccl_stub.h"
#include "engine/endpoint_test_utils.h"
#include "engine/test_mmpa_utils.h"
#include "hccl/hccl_types.h"
#include "hcomm/hcomm_res_defs.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
constexpr int32_t kUbMemEndpointPoolDevId = 910270;

EndpointDesc MakeEndpoint(CommProtocol protocol, EndpointLocType loc_type) {
  EndpointDesc ep{};
  ep.protocol = protocol;
  ep.loc.locType = loc_type;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 1U;
  return ep;
}
}  // namespace

TEST(EndpointFactoryUt, CreateSelectsUbMemEndpoint) {
  auto endpoint = Endpoint::Create(MakeEndpoint(COMM_PROTOCOL_UB_MEM, ENDPOINT_LOC_TYPE_HOST));
  EXPECT_NE(dynamic_cast<UbMemEndpoint *>(endpoint.get()), nullptr);
}

TEST(EndpointFactoryUt, CreateSelectsHcommEndpoint) {
  auto endpoint = Endpoint::Create(MakeEndpoint(COMM_PROTOCOL_ROCE, ENDPOINT_LOC_TYPE_DEVICE));
  EXPECT_NE(dynamic_cast<HcommEndpoint *>(endpoint.get()), nullptr);
}

TEST(EndpointFactoryUt, UbMemEndpointHasNoListenPort) {
  auto acl_stub = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", kUbMemEndpointPoolDevId, 0, 9, 8);
  llm::AclRuntimeStub::SetInstance(acl_stub);
  hixl_test::InstallSysApiHooks(std::make_shared<test::KernelJsonMmpaStub>());
  VirtualMemoryManager::GetInstance().Finalize();

  auto endpoint = Endpoint::Create(MakeEndpoint(COMM_PROTOCOL_UB_MEM, ENDPOINT_LOC_TYPE_DEVICE));
  ASSERT_NE(endpoint, nullptr);
  ASSERT_EQ(endpoint->Initialize(), SUCCESS);
  uint32_t port = 0U;
  EXPECT_EQ(endpoint->GetListenPort(port), SUCCESS);
  EXPECT_EQ(port, 0U);
  EXPECT_EQ(endpoint->Finalize(), SUCCESS);

  VirtualMemoryManager::GetInstance().Finalize();
  hixl_test::ResetSysApiHooks();
  llm::AclRuntimeStub::Reset();
}

}  // namespace hixl
