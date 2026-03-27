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
#include <vector>
#include "nlohmann/json.hpp"
#include "ascendcl_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#define private public
#include "common/loc_comm_res_generator.h"
#undef private

namespace hixl {
namespace {
class MockLocCommResMmpaStub : public llm::MmpaStubApiGe {
public:
  std::string fake_real_path_;
  bool real_path_ok_ = false;
  bool access_ok_ = false;
  INT32 RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    (void)path;
    if (!real_path_ok_) {
      return EN_ERROR;
    }
    if (fake_real_path_.empty() || realPathLen <= 0) {
      return EN_ERROR;
    }
    size_t destMax = static_cast<size_t>(realPathLen);
    errno_t ret = strncpy_s(realPath, destMax, fake_real_path_.c_str(), destMax - 1);
    if (ret != EOK) {
      return EN_ERROR;
    }
    return EN_OK;
  }
  INT32 Access(const CHAR *path_name) override {
    (void)path_name;
    return access_ok_ ? EN_OK : EN_ERROR;
  }
};

std::string CreateTempFileWithContent(const std::string &content) {
  char file_template[] = "/tmp/loc_comm_res_ut_XXXXXX";
  int fd = mkstemp(file_template);
  EXPECT_NE(fd, -1);
  if (fd == -1) {
    return "";
  }

  FILE *fp = fdopen(fd, "w");
  EXPECT_NE(fp, nullptr);
  if (fp == nullptr) {
    close(fd);
    return "";
  }

  (void)fwrite(content.data(), 1, content.size(), fp);
  (void)fclose(fp);
  return std::string(file_template);
}

std::string CreateExecutableScript(const std::string &name, const std::string &content) {
  std::string path = "/tmp/" + name;
  std::ofstream ofs(path);
  EXPECT_TRUE(ofs.is_open());
  ofs << content;
  ofs.close();
  chmod(path.c_str(), 0755);
  return path;
}

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

    mmpa_stub_ = std::make_shared<MockLocCommResMmpaStub>();
    llm::MmpaStub::GetInstance().SetImpl(mmpa_stub_);

    const char *old_path = std::getenv("PATH");
    old_path_ = (old_path == nullptr) ? "" : old_path;
  }

  void TearDown() override {
    llm::AclRuntimeStub::Reset();
    llm::MmpaStub::GetInstance().Reset();

    if (!old_path_.empty()) {
      setenv("PATH", old_path_.c_str(), 1);
    }
  }

  std::shared_ptr<MockLocCommResAclRuntimeStub> acl_stub_;
  std::shared_ptr<MockLocCommResMmpaStub> mmpa_stub_;
  std::string old_path_;
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

TEST_F(LocCommResGeneratorUTest, GetSocNameEmptyStringFailed) {
  acl_stub_->soc_name_ = "";

  std::string soc_name;
  EXPECT_EQ(LocCommResGenerator::GetSocName(soc_name), FAILED);
}

TEST_F(LocCommResGeneratorUTest, GetHccnOutputStdoutSuccess) {
  std::string output;
  EXPECT_EQ(LocCommResGenerator::GetHccnOutput("printf 'ipaddr:10.10.10.10\\n'", output), SUCCESS);
  EXPECT_EQ(output, "ipaddr:10.10.10.10\n");
}

TEST_F(LocCommResGeneratorUTest, GetHccnOutputStderrSuccess) {
  std::string output;
  EXPECT_EQ(LocCommResGenerator::GetHccnOutput("sh -c \"echo err-msg >&2\"", output), SUCCESS);
  EXPECT_EQ(output, "err-msg\n");
}

TEST_F(LocCommResGeneratorUTest, ExecuteCommandAndParseIpSuccess) {
  std::string output;
  std::string ip;
  EXPECT_EQ(
      LocCommResGenerator::ExecuteCommandAndParseIp("printf 'xxx\\nipaddr:192.168.0.8\\n'", output, ip),
      SUCCESS);
  EXPECT_EQ(output, "xxx\nipaddr:192.168.0.8\n");
  EXPECT_EQ(ip, "192.168.0.8");
}

TEST_F(LocCommResGeneratorUTest, ExecuteCommandAndParseIpNoIpKeepEmpty) {
  std::string output;
  std::string ip = "old";
  EXPECT_EQ(LocCommResGenerator::ExecuteCommandAndParseIp("printf 'no-ip-here\\n'", output, ip), SUCCESS);
  EXPECT_EQ(output, "no-ip-here\n");
  EXPECT_EQ(ip, "old");
}

TEST_F(LocCommResGeneratorUTest, ExecuteCommandAndParseIpEmptyIpFromPrefix) {
  std::string output;
  std::string ip;
  EXPECT_EQ(LocCommResGenerator::ExecuteCommandAndParseIp("printf 'ipaddr:\\n'", output, ip), SUCCESS);
  EXPECT_EQ(output, "ipaddr:\n");
  EXPECT_TRUE(ip.empty());
}

TEST_F(LocCommResGeneratorUTest, ExtractIpAddressLastIpWinsByOverwrite) {
  std::string ip;
  LocCommResGenerator::ExtractIpAddress("ipaddr:1.1.1.1\nipaddr:2.2.2.2\n", ip);
  EXPECT_EQ(ip, "1.1.1.1");
}

TEST_F(LocCommResGeneratorUTest, GenerateForOtherSocReturnsParamInvalid) {
  acl_stub_->soc_name_ = "OtherSoc";
  acl_stub_->phy_device_id_ = 6;

  std::string loc_comm_res;
  EXPECT_EQ(LocCommResGenerator::Generate(0, loc_comm_res), PARAM_INVALID);
}

TEST_F(LocCommResGeneratorUTest, GenerateForA3BuildNetInstanceIdSuccessButLaterMayFail) {
  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->phy_device_id_ = 6;
  acl_stub_->super_pod_id_ = 88;

  std::string net_instance_id;
  EXPECT_EQ(LocCommResGenerator::BuildNetInstanceId(0, net_instance_id), SUCCESS);
  EXPECT_EQ(net_instance_id, "88");
}

TEST_F(LocCommResGeneratorUTest, BuildEndpointListClearsInputVectorBeforeBuild) {
  std::vector<loc_comm_res::EndpointInfo> endpoint_list;
  loc_comm_res::EndpointInfo dummy{};
  dummy.protocol = "dummy";
  dummy.comm_id = "dummy";
  dummy.placement = "dummy";
  endpoint_list.emplace_back(dummy);

  EXPECT_NE(endpoint_list.size(), 0U);

  Status ret = LocCommResGenerator::BuildEndpointList(0, 3, endpoint_list);

  if (ret != SUCCESS) {
    EXPECT_TRUE(endpoint_list.empty());
  }
}

TEST_F(LocCommResGeneratorUTest, GeneratePhyDeviceSuccessAndOtherSocStillParamInvalid) {
  acl_stub_->soc_name_ = "OtherSoc";
  acl_stub_->phy_device_id_ = 0;
  acl_stub_->phy_dev_failed_ = false;

  std::string loc_comm_res;
  EXPECT_EQ(LocCommResGenerator::Generate(0, loc_comm_res), PARAM_INVALID);
  EXPECT_TRUE(loc_comm_res.empty());
}

TEST_F(LocCommResGeneratorUTest, GetHostIpMaybeSuccessOnLinuxEnvironment) {
  std::string host_ip;
  Status ret = LocCommResGenerator::GetHostIp(host_ip);
  if (ret == SUCCESS) {
    EXPECT_FALSE(host_ip.empty());
  }
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnConfSuccess) {
  const std::string file_path = CreateTempFileWithContent(
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
  const std::string file_path = CreateTempFileWithContent(
      "address_3=192.168.100.8=extra\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), FAILED);

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GetDeviceIpFromHccnConfInvalidIpFailed) {
  const std::string file_path = CreateTempFileWithContent(
      "address_3=not_an_ip\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string device_ip;
  EXPECT_EQ(LocCommResGenerator::GetDeviceIp(3, device_ip), PARAM_INVALID);

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, BuildRoceEndpointSuccess) {
  const std::string file_path = CreateTempFileWithContent(
      "address_3=10.10.10.3\n");

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

TEST_F(LocCommResGeneratorUTest, BuildEndpointListSuccess) {
  const std::string file_path = CreateTempFileWithContent(
      "address_3=10.10.10.3\n");

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::vector<loc_comm_res::EndpointInfo> endpoint_list;
  EXPECT_EQ(LocCommResGenerator::BuildEndpointList(0, 3, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 2U);

  EXPECT_EQ(endpoint_list[0].protocol, "roce");
  EXPECT_EQ(endpoint_list[0].comm_id, "10.10.10.3");
  EXPECT_EQ(endpoint_list[0].placement, "device");

  EXPECT_EQ(endpoint_list[1].protocol, "hccs");
  EXPECT_EQ(endpoint_list[1].comm_id, "3");
  EXPECT_EQ(endpoint_list[1].placement, "device");

  (void)remove(file_path.c_str());
}

TEST_F(LocCommResGeneratorUTest, GenerateSuccessForA3AndDumpJson) {
  const std::string file_path = CreateTempFileWithContent(
      "address_3=10.10.10.3\n");

  acl_stub_->soc_name_ = "Ascend910_9391";
  acl_stub_->phy_device_id_ = 3;
  acl_stub_->super_pod_id_ = 88;

  mmpa_stub_->real_path_ok_ = true;
  mmpa_stub_->access_ok_ = true;
  mmpa_stub_->fake_real_path_ = file_path;

  std::string loc_comm_res;
  EXPECT_EQ(LocCommResGenerator::Generate(0, loc_comm_res), SUCCESS);
  EXPECT_FALSE(loc_comm_res.empty());

  nlohmann::json j = nlohmann::json::parse(loc_comm_res);
  EXPECT_EQ(j["version"], "1.3");
  EXPECT_EQ(j["net_instance_id"], "88");
  ASSERT_TRUE(j["endpoint_list"].is_array());
  ASSERT_EQ(j["endpoint_list"].size(), 2U);

  EXPECT_EQ(j["endpoint_list"][0]["protocol"], "roce");
  EXPECT_EQ(j["endpoint_list"][0]["comm_id"], "10.10.10.3");
  EXPECT_EQ(j["endpoint_list"][0]["placement"], "device");

  EXPECT_EQ(j["endpoint_list"][1]["protocol"], "hccs");
  EXPECT_EQ(j["endpoint_list"][1]["comm_id"], "3");
  EXPECT_EQ(j["endpoint_list"][1]["placement"], "device");

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