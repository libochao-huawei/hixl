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

#include <memory>
#include <string>
#include <vector>

#include "ascendcl_stub.h"

#define private public
#include "common/loc_comm_res_generator.h"
#undef private

namespace hixl {
namespace {
class MockLocCommResAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  std::string soc_name_ = "Ascend910_9391";
  int32_t phy_device_id_ = 3;
  int64_t super_pod_id_ = 8;
  bool return_null_soc_name_ = false;
  bool phy_dev_failed_ = false;
  bool device_info_failed_ = false;

  const char *aclrtGetSocName() override {
    if (return_null_soc_name_) {
      return nullptr;
    }
    return soc_name_.c_str();
  }

  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) override {
    (void)logicDevId;
    if (phy_dev_failed_ || phyDevId == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    *phyDevId = phy_device_id_;
    return ACL_SUCCESS;
  }

  aclError aclrtGetDeviceInfo(uint32_t deviceId, aclrtDevAttr attr, int64_t *value) override {
    (void)deviceId;
    if (device_info_failed_ || value == nullptr) {
      return ACL_ERROR_FAILURE;
    }
    if (attr == ACL_DEV_ATTR_SUPER_POD_ID) {
      *value = super_pod_id_;
      return ACL_SUCCESS;
    }
    *value = 0;
    return ACL_SUCCESS;
  }
};
}  // namespace

class LocCommResGeneratorUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = std::make_shared<MockLocCommResAclRuntimeStub>();
    llm::AclRuntimeStub::SetInstance(acl_stub_);
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
  }

  std::shared_ptr<MockLocCommResAclRuntimeStub> acl_stub_;
};

TEST_F(LocCommResGeneratorUTest, IsA2SocSuccess) {
  EXPECT_TRUE(LocCommResGenerator::IsA2Soc("Ascend910B1"));
  EXPECT_FALSE(LocCommResGenerator::IsA2Soc("Ascend910_9391"));
  EXPECT_FALSE(LocCommResGenerator::IsA2Soc("OtherSoc"));
}

TEST_F(LocCommResGeneratorUTest, IsA3SocSuccess) {
  EXPECT_TRUE(LocCommResGenerator::IsA3Soc("Ascend910_9391"));
  EXPECT_TRUE(LocCommResGenerator::IsA3Soc("Ascend910_9381"));
  EXPECT_TRUE(LocCommResGenerator::IsA3Soc("Ascend910_9392"));
  EXPECT_FALSE(LocCommResGenerator::IsA3Soc("Ascend910B1"));
  EXPECT_FALSE(LocCommResGenerator::IsA3Soc("OtherSoc"));
}

TEST_F(LocCommResGeneratorUTest, ExtractIpAddressSuccess) {
  std::string ip;
  LocCommResGenerator::ExtractIpAddress("xxx\nipaddr:192.168.1.10\nxxx\n", ip);
  EXPECT_EQ(ip, "192.168.1.10");
}

TEST_F(LocCommResGeneratorUTest, ExtractIpAddressNoPrefixKeepEmpty) {
  std::string ip;
  LocCommResGenerator::ExtractIpAddress("no ip here", ip);
  EXPECT_TRUE(ip.empty());
}

TEST_F(LocCommResGeneratorUTest, BuildHccsEndpointSuccess) {
  loc_comm_res::EndpointInfo endpoint{};
  EXPECT_EQ(LocCommResGenerator::BuildHccsEndpoint(12, endpoint), SUCCESS);
  EXPECT_EQ(endpoint.protocol, "hccs");
  EXPECT_EQ(endpoint.comm_id, "12");
  EXPECT_EQ(endpoint.placement, "device");
}

TEST_F(LocCommResGeneratorUTest, GetSocNameSuccess) {
  acl_stub_->soc_name_ = "Ascend910_9391";

  std::string soc_name;
  EXPECT_EQ(LocCommResGenerator::GetSocName(soc_name), SUCCESS);
  EXPECT_EQ(soc_name, "Ascend910_9391");
}

TEST_F(LocCommResGeneratorUTest, GetSocNameNullptrFailed) {
  acl_stub_->return_null_soc_name_ = true;

  std::string soc_name;
  EXPECT_EQ(LocCommResGenerator::GetSocName(soc_name), FAILED);
}

TEST_F(LocCommResGeneratorUTest, BuildNetInstanceIdForA3Success) {
  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->super_pod_id_ = 12345;

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, net_instance_id), SUCCESS);
  EXPECT_EQ(net_instance_id, "12345");
}

TEST_F(LocCommResGeneratorUTest, BuildNetInstanceIdForA3DeviceInfoFailed) {
  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->device_info_failed_ = true;

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, net_instance_id), FAILED);
}

TEST_F(LocCommResGeneratorUTest, BuildNetInstanceIdForOtherSocFailed) {
  acl_stub_->soc_name_ = "OtherSoc";

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, net_instance_id), PARAM_INVALID);
}

TEST_F(LocCommResGeneratorUTest, BuildEndpointListPhyDeviceFailed) {
  acl_stub_->phy_dev_failed_ = true;

  std::vector<loc_comm_res::EndpointInfo> endpoint_list;
  EXPECT_EQ(LocCommResGenerator::BuildEndpointList(0, 3, endpoint_list), FAILED);
}

TEST_F(LocCommResGeneratorUTest, GeneratePhyDeviceFailed) {
  acl_stub_->phy_dev_failed_ = true;

  std::string loc_comm_res;
  EXPECT_EQ(LocCommResGenerator::Generate(0, loc_comm_res), FAILED);
}
}  // namespace hixl