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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "nlohmann/json.hpp"
#include "ascendcl_stub.h"
#include "test_mmpa_utils.h"
#include "depends/mmpa/src/mmpa_stub.h"

#define private public
#include "common/loc_comm_res_generator.h"
#undef private

namespace hixl {
namespace {
using MockLocCommResMmpaStub = test::TestMmpaStub;

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

std::string CreateExecutableScript(const std::string &name, const std::string &content) {
  std::string path = "/tmp/" + name;
  std::ofstream ofs(path);
  EXPECT_TRUE(ofs.is_open());
  ofs << content;
  ofs.close();
  chmod(path.c_str(), 0755);
  return path;
}
}  // namespace

class LocCommResGeneratorUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = std::make_shared<MockLocCommResAclRuntimeStub>();
    llm::AclRuntimeStub::SetInstance(acl_stub_);

    mmpa_stub_ = std::make_shared<MockLocCommResMmpaStub>();
    llm::MmpaStub::GetInstance().SetImpl(mmpa_stub_);

    const char *old_path = std::getenv("PATH");
    old_path_ = (old_path == nullptr) ? "" : old_path;
    old_intra_roce_enable_ = std::getenv("HCCL_INTRA_ROCE_ENABLE");
    unsetenv("HCCL_INTRA_ROCE_ENABLE");
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
    llm::MmpaStub::GetInstance().Reset();

    if (!old_path_.empty()) {
      setenv("PATH", old_path_.c_str(), 1);
    }
    if (old_intra_roce_enable_ != nullptr) {
      setenv("HCCL_INTRA_ROCE_ENABLE", old_intra_roce_enable_, 1);
    } else {
      unsetenv("HCCL_INTRA_ROCE_ENABLE");
    }
  }

  std::shared_ptr<MockLocCommResAclRuntimeStub> acl_stub_;
  std::shared_ptr<MockLocCommResMmpaStub> mmpa_stub_;
  std::string old_path_;
  const char *old_intra_roce_enable_ = nullptr;
};

TEST_F(LocCommResGeneratorUTest, BuildHccsEndpointSuccess) {
  loc_comm_res::EndpointInfo endpoint{};
  EXPECT_EQ(LocCommResGenerator::BuildHccsEndpoint(12, endpoint), SUCCESS);
  EXPECT_EQ(endpoint.protocol, "hccs");
  EXPECT_EQ(endpoint.comm_id, "12");
  EXPECT_EQ(endpoint.placement, "device");
}

TEST_F(LocCommResGeneratorUTest, BuildNetInstanceIdForV3Success) {
  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->super_pod_id_ = 12345;

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, "127.0.0.1:26000", net_instance_id), SUCCESS);
  EXPECT_EQ(net_instance_id, "12345");
}

TEST_F(LocCommResGeneratorUTest, BuildNetInstanceIdForV2Success) {
  acl_stub_->soc_name_ = "Ascend910B1";

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, "192.168.1.8:26000", net_instance_id), SUCCESS);
  EXPECT_EQ(net_instance_id, "192.168.1.8");
}

TEST_F(LocCommResGeneratorUTest, BuildNetInstanceIdForV2WildcardFailed) {
  acl_stub_->soc_name_ = "Ascend910B1";

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, "0.0.0.0:26000", net_instance_id), PARAM_INVALID);
}

TEST_F(LocCommResGeneratorUTest, BuildNetInstanceIdForOtherSocFailed) {
  acl_stub_->soc_name_ = "OtherSoc";

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, "127.0.0.1:26000", net_instance_id), PARAM_INVALID);
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnConfSuccess) {
  const std::string file_path = test::CreateTempFileWithContent(
      "/tmp/loc_comm_res_ut_XXXXXX",
      "address_3=192.168.100.8\n"
      "address_4=192.168.100.9\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "192.168.100.8");

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnConfInvalidFormatFailed) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=192.168.100.8=extra\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), FAILED);

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnConfInvalidIpFailed) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=not_an_ip\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), PARAM_INVALID);

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, BuildRoceEndpointSuccess) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  loc_comm_res::EndpointInfo endpoint{};
  EXPECT_EQ(LocCommResGenerator::BuildRoceEndpoint(3, endpoint), SUCCESS);
  EXPECT_EQ(endpoint.protocol, "roce");
  EXPECT_EQ(endpoint.comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint.placement, "device");

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, BuildRoceEndpointEmptyIpFailed) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;
  setenv("PATH", "/tmp/loc_comm_res_empty_path", 1);
  mkdir("/tmp/loc_comm_res_empty_path", 0755);

  loc_comm_res::EndpointInfo endpoint{};
  EXPECT_EQ(LocCommResGenerator::BuildRoceEndpoint(3, endpoint), FAILED);
}

TEST_F(LocCommResGeneratorUTest, BuildEndpointListSuccess) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::vector<loc_comm_res::EndpointInfo> endpoint_list;
  EXPECT_EQ(LocCommResGenerator::BuildEndpointList(3, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 2U);
  EXPECT_EQ(endpoint_list[0].protocol, "roce");
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[1].protocol, "hccs");
  EXPECT_EQ(endpoint_list[1].comm_id, "3");

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, BuildEndpointListWithIntraRoceEnabledOnlyReturnsRoce) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;
  setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);

  std::vector<loc_comm_res::EndpointInfo> endpoint_list;
  EXPECT_EQ(LocCommResGenerator::BuildEndpointList(3, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);
  EXPECT_EQ(endpoint_list[0].protocol, "roce");

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GenerateInfoSuccessForV3) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->phy_device_id_ = 3;
  acl_stub_->super_pod_id_ = 88;
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  loc_comm_res::LocCommResInfo info{};
  EXPECT_EQ(LocCommResGenerator::GenerateInfo(0, "127.0.0.1:26000", info), SUCCESS);
  EXPECT_EQ(info.version, "1.3");
  EXPECT_EQ(info.net_instance_id, "88");
  ASSERT_EQ(info.endpoint_list.size(), 2U);

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GenerateSuccessForV3AndDumpJson) {
  const std::string file_path =
      test::CreateTempFileWithContent("/tmp/loc_comm_res_ut_XXXXXX", "address_3=10.10.10.3\n");

  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->phy_device_id_ = 3;
  acl_stub_->super_pod_id_ = 88;
  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string loc_comm_res;
  EXPECT_EQ(LocCommResGenerator::Generate(0, "127.0.0.1:26000", loc_comm_res), SUCCESS);
  EXPECT_FALSE(loc_comm_res.empty());

  nlohmann::json j = nlohmann::json::parse(loc_comm_res);
  EXPECT_EQ(j["version"], "1.3");
  EXPECT_EQ(j["net_instance_id"], "88");
  ASSERT_EQ(j["endpoint_list"].size(), 2U);

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnToolSuccess) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;

  const std::string tool_path = CreateExecutableScript(
      "hccn_tool",
      "#!/bin/sh\n"
      "echo \"ipaddr:172.16.1.20\"\n");

  setenv("PATH", "/tmp", 1);

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), SUCCESS);
  EXPECT_EQ(device_ip, "172.16.1.20");

  (void)remove(tool_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnToolInvalidIpFailed) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;

  const std::string tool_path = CreateExecutableScript(
      "hccn_tool",
      "#!/bin/sh\n"
      "echo \"ipaddr:not_an_ip\"\n");

  setenv("PATH", "/tmp", 1);

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), PARAM_INVALID);

  (void)remove(tool_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnToolEmptyIpFailed) {
  mmpa_stub_->real_path_ok_ = false;
  mmpa_stub_->access_ok_ = false;

  const std::string tool_path = CreateExecutableScript(
      "hccn_tool",
      "#!/bin/sh\n"
      "echo \"no_ip_here\"\n");

  setenv("PATH", "/tmp", 1);

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), FAILED);

  (void)remove(tool_path.c_str());
}
}  // namespace hixl
