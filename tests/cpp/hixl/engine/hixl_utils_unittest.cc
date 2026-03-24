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
#include <gmock/gmock.h>
#include <limits>
#include "common/hixl_utils.h"

using namespace ::testing;

namespace hixl {
class HixlUtilsUTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(HixlUtilsUTest, ParseEidAddressSuccessTest) {
  CommAddr addr;
  std::string eid_str = "00010002000300040005000600070008";
  Status st = ParseEidAddress(eid_str, addr);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(addr.type, COMM_ADDR_TYPE_EID);

  // 验证解析结果
  EXPECT_EQ(addr.eid[0], 0x00);
  EXPECT_EQ(addr.eid[1], 0x01);
  EXPECT_EQ(addr.eid[2], 0x00);
  EXPECT_EQ(addr.eid[3], 0x02);
  EXPECT_EQ(addr.eid[4], 0x00);
  EXPECT_EQ(addr.eid[5], 0x03);
  EXPECT_EQ(addr.eid[6], 0x00);
  EXPECT_EQ(addr.eid[7], 0x04);
  EXPECT_EQ(addr.eid[8], 0x00);
  EXPECT_EQ(addr.eid[9], 0x05);
  EXPECT_EQ(addr.eid[10], 0x00);
  EXPECT_EQ(addr.eid[11], 0x06);
  EXPECT_EQ(addr.eid[12], 0x00);
  EXPECT_EQ(addr.eid[13], 0x07);
  EXPECT_EQ(addr.eid[14], 0x00);
  EXPECT_EQ(addr.eid[15], 0x08);
}

// ParseEidAddress 函数测试：正常场景 - 包含大小写字母的十六进制段
TEST_F(HixlUtilsUTest, ParseEidAddressMixedCaseTest) {
  CommAddr addr;
  std::string eid_str = "aBcD1234567890EfAbCdEf1234567890";
  Status st = ParseEidAddress(eid_str, addr);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(addr.type, COMM_ADDR_TYPE_EID);

  // 验证解析结果
  EXPECT_EQ(addr.eid[0], 0xAB);
  EXPECT_EQ(addr.eid[1], 0xCD);
  EXPECT_EQ(addr.eid[2], 0x12);
  EXPECT_EQ(addr.eid[3], 0x34);
  EXPECT_EQ(addr.eid[4], 0x56);
  EXPECT_EQ(addr.eid[5], 0x78);
  EXPECT_EQ(addr.eid[6], 0x90);
  EXPECT_EQ(addr.eid[7], 0xEF);
  EXPECT_EQ(addr.eid[8], 0xAB);
  EXPECT_EQ(addr.eid[9], 0xCD);
  EXPECT_EQ(addr.eid[10], 0xEF);
  EXPECT_EQ(addr.eid[11], 0x12);
  EXPECT_EQ(addr.eid[12], 0x34);
  EXPECT_EQ(addr.eid[13], 0x56);
  EXPECT_EQ(addr.eid[14], 0x78);
  EXPECT_EQ(addr.eid[15], 0x90);
}

// ParseEidAddress 函数测试：异常场景 - 长度不足32个字符
TEST_F(HixlUtilsUTest, ParseEidAddressShortStringTest) {
  CommAddr addr;
  std::string eid_str = "0001000200030004000500060007";
  Status st = ParseEidAddress(eid_str, addr);
  EXPECT_EQ(st, PARAM_INVALID);
}

// ParseEidAddress 函数测试：异常场景 - 长度超过32个字符
TEST_F(HixlUtilsUTest, ParseEidAddressLongStringTest) {
  CommAddr addr;
  std::string eid_str = "000100020003000400050006000700080009";
  Status st = ParseEidAddress(eid_str, addr);
  EXPECT_EQ(st, PARAM_INVALID);
}

// ParseEidAddress 函数测试：异常场景 - 包含非十六进制字符
TEST_F(HixlUtilsUTest, ParseEidAddressNonHexTest) {
  CommAddr addr;
  // 测试包含非十六进制字符的EID地址
  std::string eid_str = "0001000200030004000500060007000g";
  Status st = ParseEidAddress(eid_str, addr);
  EXPECT_EQ(st, PARAM_INVALID);
}

// ParseEidAddress 函数测试：异常场景 - 空字符串
TEST_F(HixlUtilsUTest, ParseEidAddressEmptyStringTest) {
  CommAddr addr;
  // 测试空字符串
  std::string eid_str = "";
  Status st = ParseEidAddress(eid_str, addr);
  EXPECT_EQ(st, PARAM_INVALID);
}
TEST_F(HixlUtilsUTest, EndpointConfigToStringContainsDeviceInfoTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.plane = "plane-a";
  ep.dst_eid = "00010002000300040005000600070008";
  ep.net_instance_id = "superpod_1";
  ep.device_info.phy_device_id = 3;
  ep.device_info.super_device_id = 7;
  ep.device_info.super_pod_id = 9;

  const std::string text = ep.ToString();
  EXPECT_THAT(text, HasSubstr("protocol: roce"));
  EXPECT_THAT(text, HasSubstr("comm_id: 127.0.0.1"));
  EXPECT_THAT(text, HasSubstr("placement: device"));
  EXPECT_THAT(text, HasSubstr("net_instance_id: superpod_1"));
  EXPECT_THAT(text, HasSubstr("device_info: DeviceInfoConfig{"));
  EXPECT_THAT(text, HasSubstr("phy_device_id: 3"));
  EXPECT_THAT(text, HasSubstr("super_device_id: 7"));
  EXPECT_THAT(text, HasSubstr("super_pod_id: 9"));
}

TEST_F(HixlUtilsUTest, ConvertToEndpointDescDeviceRoceUseDeviceInfoTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 3;
  ep.device_info.super_device_id = 7;
  ep.device_info.super_pod_id = 9;

  EndpointDesc endpoint{};
  Status st = ConvertToEndpointDesc(ep, endpoint, 11U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_ROCE);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 3U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 7U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 9U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(HixlUtilsUTest, ConvertToEndpointDescDeviceRoceFallbackPhyIdTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = -1;
  ep.device_info.super_device_id = -1;
  ep.device_info.super_pod_id = -1;

  EndpointDesc endpoint{};
  Status st = ConvertToEndpointDesc(ep, endpoint, 15U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_ROCE);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 15U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 0U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 0U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(HixlUtilsUTest, ConvertToEndpointDescDeviceHccsUseDeviceInfoTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolHccs;
  ep.comm_id = "5";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 2;
  ep.device_info.super_device_id = 4;
  ep.device_info.super_pod_id = 8;

  EndpointDesc endpoint{};
  Status st = ConvertToEndpointDesc(ep, endpoint, 10U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_HCCS);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.commAddr.id, 5U);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 2U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 4U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 8U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(HixlUtilsUTest, ConvertToEndpointDescDeviceHccsInvalidCommIdTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolHccs;
  ep.comm_id = "abc";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 2;
  ep.device_info.super_device_id = 4;
  ep.device_info.super_pod_id = 8;

  EndpointDesc endpoint{};
  Status st = ConvertToEndpointDesc(ep, endpoint, 10U);
  EXPECT_EQ(st, PARAM_INVALID);
}

TEST_F(HixlUtilsUTest, ConvertToEndpointDescDeviceUbKeepLegacyLogicTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolUbCtp;
  ep.comm_id = "00010002000300040005000600070008";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 123;
  ep.device_info.super_device_id = 456;
  ep.device_info.super_pod_id = 789;

  EndpointDesc endpoint{};
  Status st = ConvertToEndpointDesc(ep, endpoint, 6U);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(endpoint.protocol, COMM_PROTOCOL_UBC_CTP);
  EXPECT_EQ(endpoint.loc.locType, ENDPOINT_LOC_TYPE_DEVICE);
  EXPECT_EQ(endpoint.loc.device.devPhyId, 6U);
  EXPECT_EQ(endpoint.loc.device.superDevId, 0U);
  EXPECT_EQ(endpoint.loc.device.superPodIdx, 0U);
  EXPECT_EQ(endpoint.loc.device.serverIdx, 0U);
}

TEST_F(HixlUtilsUTest, ConvertToEndpointDescDeviceRoceSuperDeviceIdOutOfRangeTest) {
  EndpointConfig ep;
  ep.protocol = kProtocolRoce;
  ep.comm_id = "127.0.0.1";
  ep.placement = kPlacementDevice;
  ep.device_info.phy_device_id = 3;
  ep.device_info.super_device_id = static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1;
  ep.device_info.super_pod_id = 9;

  EndpointDesc endpoint{};
  Status st = ConvertToEndpointDesc(ep, endpoint, 11U);
  EXPECT_EQ(st, PARAM_INVALID);
}
}  // namespace hixl