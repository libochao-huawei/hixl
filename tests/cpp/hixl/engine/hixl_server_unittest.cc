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
#include "engine/hixl_server.h"
#include "hixl/hixl_types.h"
#include "common/hixl_inner_types.h"
#include "common/hixl_utils.h"


namespace hixl {

static constexpr uint32_t kMemAddr1 = 0x1000;
static constexpr uint32_t kMemAddr2 = 0x1080; // Overlaps
static constexpr uint32_t kMemAddr3 = 0x1100; // No overlap
static constexpr uint32_t kMemLen = 0x100;
static constexpr uint32_t kPort = 16000;

class HixlServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EndPointConfig ep0;
    ep0.protocol = "roce";
    ep0.comm_id = "192.10.1.2";
    ep0.placement = "device";
    EndPointConfig ep1;
    ep1.protocol = "roce";
    ep1.comm_id = "192.10.1.1";
    ep1.placement = "host";

    default_eps.emplace_back(ep0);
    default_eps.emplace_back(ep1);

    mem_.addr = kMemAddr1;
    mem_.len = kMemLen;
  }

  void TearDown() override {
  }
private:
  HixlServer server_;
  std::vector<EndPointConfig> default_eps;
  MemDesc mem_{};
  std::string ip_ = "127.0.0.1";
  int32_t port_ = kPort;
};

TEST_F(HixlServerTest, RegisterMemWithoutInit) {
  MemHandle handle = nullptr;
  Status ret = server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(HixlServerTest, FinalizeWithoutInit) {
  Status ret = server_.Finalize();
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlServerTest, InitializePortZero) {
  EXPECT_EQ(server_.Initialize(ip_, 0, default_eps), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemSameTwice) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle1), SUCCESS);
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle2), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemOverlap) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemDesc mem1{};
  mem1.addr = kMemAddr1;
  mem1.len = kMemLen;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem1, MemType::MEM_DEVICE, handle1), SUCCESS);
  MemDesc mem2{};
  mem2.addr = kMemAddr2;
  mem2.len = kMemLen;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem2, MemType::MEM_DEVICE, handle2), PARAM_INVALID);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, DeregisterMemDoubleFree) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemHandle handle = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), SUCCESS);

  MemHandle handle_copy = handle;
  EXPECT_EQ(server_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(handle, nullptr);

  // Using the copy
  EXPECT_EQ(server_.DeregisterMem(handle_copy), SUCCESS);

  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemBoundary) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);

  MemDesc mem1{};
  mem1.addr = kMemAddr1;
  mem1.len = kMemLen;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem1, MemType::MEM_DEVICE, handle1), SUCCESS);

  MemDesc mem2{};
  mem2.addr = kMemAddr3; // Starts exactly at end of mem1
  mem2.len = kMemLen;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem2, MemType::MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterMemOverlapDifferentType) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);

  MemDesc mem1{};
  mem1.addr = kMemAddr1;
  mem1.len = kMemLen;
  MemHandle handle1 = nullptr;
  MemDesc mem2{};
  mem2.addr = kMemAddr2; // Overlaps
  mem2.len = kMemLen;
  MemHandle handle2 = nullptr;
  // This should SUCCEED because types are different
  EXPECT_EQ(server_.RegisterMem(mem1, MemType::MEM_DEVICE, handle1), SUCCESS);
  EXPECT_EQ(server_.RegisterMem(mem2, MemType::MEM_HOST, handle2), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, DeregisterNonExistent) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);

  MemHandle handle = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), SUCCESS);

  MemHandle invalid_handle = reinterpret_cast<MemHandle>(0xdeadbeef);
  EXPECT_NE(server_.DeregisterMem(invalid_handle), FAILED);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

TEST_F(HixlServerTest, RegisterAfterFinalize) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);

  MemHandle handle = nullptr;
  EXPECT_NE(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), FAILED);
}

TEST_F(HixlServerTest, NormalInitRegisterDeregisterFinalize) {
  EXPECT_EQ(server_.Initialize(ip_, port_, default_eps), SUCCESS);
  MemHandle handle = nullptr;
  EXPECT_EQ(server_.RegisterMem(mem_, MemType::MEM_DEVICE, handle), SUCCESS);
  EXPECT_NE(handle, nullptr);
  EXPECT_EQ(server_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(server_.Finalize(), SUCCESS);
}

// Tests for SerializeEndPointConfigList function
TEST(SerializeEndPointConfigListTest, SerializeEmptyList) {
  std::vector<EndPointConfig> empty_list;
  std::string result;
  Status ret = SerializeEndPointConfigList(empty_list, result);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(result, "[]");
}

TEST(SerializeEndPointConfigListTest, SerializeSingleEndpoint) {
  std::vector<EndPointConfig> list;
  EndPointConfig ep;
  ep.protocol = "roce";
  ep.comm_id = "192.168.1.1";
  ep.placement = "device";
  ep.plane = "0";
  ep.dst_eid = "1";
  ep.net_instance_id = "2";
  list.push_back(ep);

  std::string result;
  Status ret = SerializeEndPointConfigList(list, result);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(result.empty());
  // 验证JSON包含必要字段
  EXPECT_NE(result.find("protocol"), std::string::npos);
  EXPECT_NE(result.find("roce"), std::string::npos);
  EXPECT_NE(result.find("comm_id"), std::string::npos);
  EXPECT_NE(result.find("192.168.1.1"), std::string::npos);
  EXPECT_NE(result.find("placement"), std::string::npos);
  EXPECT_NE(result.find("device"), std::string::npos);
}

TEST(SerializeEndPointConfigListTest, SerializeMultipleEndpoints) {
  std::vector<EndPointConfig> list;
  EndPointConfig ep1;
  ep1.protocol = "roce";
  ep1.comm_id = "192.168.1.1";
  ep1.placement = "device";
  ep1.plane = "0";
  ep1.dst_eid = "1";
  ep1.net_instance_id = "2";

  EndPointConfig ep2;
  ep2.protocol = "tcp";
  ep2.comm_id = "192.168.1.2";
  ep2.placement = "host";
  ep2.plane = "1";
  ep2.dst_eid = "3";
  ep2.net_instance_id = "4";

  list.push_back(ep1);
  list.push_back(ep2);

  std::string result;
  Status ret = SerializeEndPointConfigList(list, result);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(result.empty());
  // 验证两个endpoint都被序列化
  EXPECT_NE(result.find("192.168.1.1"), std::string::npos);
  EXPECT_NE(result.find("192.168.1.2"), std::string::npos);
  EXPECT_NE(result.find("roce"), std::string::npos);
  EXPECT_NE(result.find("tcp"), std::string::npos);
}

TEST(ParseIpAddressTest, ValidIPv4) {
  CommAddr addr{};
  Status ret = ParseIpAddress("192.168.1.1", addr);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(addr.type, COMM_ADDR_TYPE_IP_V4);
}

TEST(ParseIpAddressTest, ValidIPv6) {
  CommAddr addr{};
  Status ret = ParseIpAddress("::1", addr);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(addr.type, COMM_ADDR_TYPE_IP_V6);
}

TEST(ParseIpAddressTest, ValidIPv6Full) {
  CommAddr addr{};
  Status ret = ParseIpAddress("2001:db8::1", addr);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(addr.type, COMM_ADDR_TYPE_IP_V6);
}

TEST(ParseIpAddressTest, InvalidAddress) {
  CommAddr addr{};
  Status ret = ParseIpAddress("invalid_ip", addr);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST(ParseIpAddressTest, EmptyAddress) {
  CommAddr addr{};
  Status ret = ParseIpAddress("", addr);
  EXPECT_EQ(ret, PARAM_INVALID);
}

// Tests for ConvertToEndPointInfo function
TEST(ConvertToEndPointInfoTest, InvalidPlacement) {
  EndPointConfig ep;
  ep.protocol = "roce";
  ep.comm_id = "192.168.1.1";
  ep.placement = "invalid_placement";

  EndPointInfo endpoint{};
  Status ret = ConvertToEndPointInfo(ep, endpoint);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST(ConvertToEndPointInfoTest, InvalidProtocol) {
  EndPointConfig ep;
  ep.protocol = "invalid_protocol";
  ep.comm_id = "192.168.1.1";
  ep.placement = "host";

  EndPointInfo endpoint{};
  Status ret = ConvertToEndPointInfo(ep, endpoint);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST(ConvertToEndPointInfoTest, RoceWithIPv6) {
  EndPointConfig ep;
  ep.protocol = "roce";
  ep.comm_id = "::1";
  ep.placement = "device";

  EndPointInfo endpoint{};
  Status ret = ConvertToEndPointInfo(ep, endpoint);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_ROCE);
  EXPECT_EQ(endpoint.location, END_POINT_LOCATION_DEVICE);
  EXPECT_EQ(endpoint.addr.type, COMM_ADDR_TYPE_IP_V6);
}

TEST(ConvertToEndPointInfoTest, RoceWithInvalidIP) {
  EndPointConfig ep;
  ep.protocol = "roce";
  ep.comm_id = "invalid_ip";
  ep.placement = "host";

  EndPointInfo endpoint{};
  Status ret = ConvertToEndPointInfo(ep, endpoint);
  EXPECT_EQ(ret, PARAM_INVALID);
}

} // namespace hixl
