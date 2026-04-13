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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <limits>
#include "common/hixl_utils.h"
#include "acl_runtime_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"

using namespace ::testing;

namespace hixl {
namespace {
constexpr const char kHccnConfPath[] = "/etc/hccn.conf";

class DeviceIpMmpaStub : public llm::MmpaStubApiGe {
 public:
  DeviceIpMmpaStub(std::string conf_path, bool conf_exists) : conf_path_(std::move(conf_path)), conf_exists_(conf_exists) {}

  int32_t RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    if (std::string(path) != kHccnConfPath || !conf_exists_) {
      return EN_ERROR;
    }
    if (realPath == nullptr || realPathLen <= 0) {
        return EN_ERROR;
    }
    size_t destMax = static_cast<size_t>(realPathLen);
    int ret = snprintf_s(realPath, destMax, destMax - 1, "%s", conf_path_.c_str());
    return (ret < 0 || ret >= realPathLen) ? EN_ERROR : EN_OK;
  }

  INT32 Access(const CHAR *path_name) override {
    if (conf_exists_ && std::string(path_name) == conf_path_ && std::filesystem::exists(conf_path_)) {
      return EN_OK;
    }
    return EN_ERROR;
  }

 private:
  std::string conf_path_;
  bool conf_exists_;
};

class QueryDeviceInfoAclRuntimeStub : public DeviceCountAclRuntimeStub {
 public:
  std::string soc_name_ = "Ascend910_9391";
  int32_t logic_device_id_ = 2;
  int32_t phy_device_id_ = 5;
  int64_t super_pod_id_ = 11;
  int64_t super_device_id_ = 13;
  int get_device_calls_ = 0;
  int get_phy_device_calls_ = 0;
  int get_context_calls_ = 0;

  const char *aclrtGetSocName() override {
    return soc_name_.c_str();
  }

  aclError aclrtGetDevice(int32_t *deviceId) override {
    ++get_device_calls_;
    if (deviceId == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *deviceId = logic_device_id_;
    return ACL_SUCCESS;
  }

  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) override {
    ++get_phy_device_calls_;
    if (phyDevId == nullptr || logicDevId != logic_device_id_) {
      return ACL_ERROR_FAILURE;
    }
    *phyDevId = phy_device_id_;
    return ACL_SUCCESS;
  }

  aclError aclrtGetDeviceInfo(uint32_t deviceId, aclrtDevAttr attr, int64_t *value) override {
    if (value == nullptr || static_cast<int32_t>(deviceId) != logic_device_id_) {
      return ACL_ERROR_FAILURE;
    }
    if (attr == ACL_DEV_ATTR_SUPER_POD_ID) {
      *value = super_pod_id_;
      return ACL_SUCCESS;
    }
    if (attr == ACL_DEV_ATTR_SUPER_POD_DEVIDE_ID) {
      *value = super_device_id_;
      return ACL_SUCCESS;
    }
    return ACL_ERROR_FAILURE;
  }

  aclError aclrtGetCurrentContext(aclrtContext *context) override {
    ++get_context_calls_;
    if (context == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *context = reinterpret_cast<aclrtContext>(0x1234);
    return ACL_SUCCESS;
  }
};
}  // namespace

class HixlUtilsUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::path("/tmp/hixl_utils_unittest");
    std::filesystem::remove_all(temp_dir_);
    std::filesystem::create_directories(temp_dir_);
    conf_path_ = temp_dir_ / "hccn.conf";
    old_path_ = getenv("PATH") == nullptr ? "" : getenv("PATH");
    llm::MmpaStub::GetInstance().Reset();
  }

  void TearDown() override {
    if (old_path_.empty()) {
      unsetenv("PATH");
    } else {
      setenv("PATH", old_path_.c_str(), 1);
    }
    llm::MmpaStub::GetInstance().Reset();
    std::filesystem::remove_all(temp_dir_);
  }

  void WriteHccnConf(const std::string &content) const {
    std::ofstream file(conf_path_);
    ASSERT_TRUE(file.is_open());
    file << content;
  }

  void InstallConfStub(bool conf_exists) const {
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<DeviceIpMmpaStub>(conf_path_.string(), conf_exists));
  }

  void CreateHccnTool(const std::string &tool_output) const {
    const auto tool_path = temp_dir_ / "hccn_tool";
    std::ofstream file(tool_path);
    ASSERT_TRUE(file.is_open());
    file << "#!/bin/sh\n";
    file << "echo \"" << tool_output << "\"\n";
    file.close();
    ASSERT_EQ(chmod(tool_path.c_str(), 0755), 0);
    const auto new_path = temp_dir_.string() + ":" + old_path_;
    setenv("PATH", new_path.c_str(), 1);
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path conf_path_;
  std::string old_path_;
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

TEST_F(HixlUtilsUTest, QueryLocalDeviceInfoHostOnlyDoesNotCallDeviceApis) {
  auto acl_stub = std::make_shared<FailingGetDeviceAclRuntimeStub>();
  acl_stub->device_count_ = 0;
  llm::AclRuntimeStub::SetInstance(acl_stub);

  LocalDeviceInfo device_info;
  EXPECT_EQ(QueryLocalDeviceInfo(device_info), SUCCESS);
  EXPECT_FALSE(device_info.has_device);
  EXPECT_EQ(device_info.logic_device_id, -1);
  EXPECT_EQ(device_info.phy_device_id, -1);
  EXPECT_EQ(acl_stub->get_device_calls_, 0);
}

TEST_F(HixlUtilsUTest, QueryLocalDeviceInfoDevicePathSuccess) {
  auto acl_stub = std::make_shared<QueryDeviceInfoAclRuntimeStub>();
  acl_stub->device_count_ = 1;
  llm::AclRuntimeStub::SetInstance(acl_stub);

  LocalDeviceInfo device_info;
  EXPECT_EQ(QueryLocalDeviceInfo(device_info), SUCCESS);
  EXPECT_TRUE(device_info.has_device);
  EXPECT_EQ(device_info.soc_type, SocType::kA3);
  EXPECT_EQ(device_info.logic_device_id, 2);
  EXPECT_EQ(device_info.phy_device_id, 5);
  EXPECT_EQ(device_info.super_pod_id, 11);
  EXPECT_EQ(device_info.super_device_id, 13);
  EXPECT_EQ(acl_stub->get_device_calls_, 1);
  EXPECT_EQ(acl_stub->get_phy_device_calls_, 1);
}

TEST_F(HixlUtilsUTest, TryGetCurrentAclContextHostOnlyReturnsNullContext) {
  auto acl_stub = std::make_shared<QueryDeviceInfoAclRuntimeStub>();
  acl_stub->device_count_ = 0;
  llm::AclRuntimeStub::SetInstance(acl_stub);

  aclrtContext context = reinterpret_cast<aclrtContext>(0x5678);
  EXPECT_EQ(TryGetCurrentAclContext(context), SUCCESS);
  EXPECT_EQ(context, nullptr);
  EXPECT_EQ(acl_stub->get_context_calls_, 0);
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

TEST_F(HixlUtilsUTest, GetDeviceIpFromHccnConfSuccessTest) {
  WriteHccnConf("address_0=192.168.1.10\naddress_1=192.168.1.11\n");
  InstallConfStub(true);

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(1, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "192.168.1.11");
}

TEST_F(HixlUtilsUTest, GetDeviceIpInvalidIpInHccnConfTest) {
  WriteHccnConf("address_0=invalid_ip\n");
  InstallConfStub(true);

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(0, device_ip), PARAM_INVALID);
}

TEST_F(HixlUtilsUTest, GetDeviceIpDoesNotFallbackWhenConfExistsButNoMatchingKeyTest) {
  WriteHccnConf("address_1=192.168.1.11\n");
  InstallConfStub(true);
  CreateHccnTool("ipaddr:10.10.10.10");

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(0, device_ip), SUCCESS);
  EXPECT_TRUE(device_ip.empty());
}

TEST_F(HixlUtilsUTest, GetDeviceIpFallbackToHccnToolWhenConfMissingTest) {
  InstallConfStub(false);
  CreateHccnTool("ipaddr:10.10.10.10");

  std::string device_ip;
  EXPECT_EQ(GetDeviceIp(0, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "10.10.10.10");
}
}  // namespace hixl
