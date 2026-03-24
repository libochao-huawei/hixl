/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "loc_comm_res_generator.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include "acl/acl.h"
#include "mmpa/mmpa_api.h"
#include "nlohmann/json.hpp"
#include "common/hixl_utils.h"

namespace hixl {
namespace {
constexpr const char kConfigVersion[] = "1.3";
constexpr uint32_t kBufferMaxSize = 128U;
constexpr const char kSocA2[] = "Ascend910B1";

const static std::set<std::string> kV2Version = {
    "Ascend910_9391",
    "Ascend910_9381",
    "Ascend910_9392",
    "Ascend910_9382",
    "Ascend910_9372",
    "Ascend910_9362"
};
}  // namespace

namespace loc_comm_res {
static void to_json(nlohmann::json &j, const EndpointInfo &e) {
  j = nlohmann::json{};
  j["protocol"] = e.protocol;
  j["comm_id"] = e.comm_id;
  j["placement"] = e.placement;
}

static void to_json(nlohmann::json &j, const LocCommResInfo &r) {
  j = nlohmann::json{};
  j["version"] = r.version;
  j["net_instance_id"] = r.net_instance_id;
  j["endpoint_list"] = r.endpoint_list;
}
}  // namespace loc_comm_res

Status LocCommResGenerator::Generate(int32_t device_id, std::string &loc_comm_res) {
  int32_t phy_device_id = 0;
  aclError acl_ret = aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id);
  if (acl_ret != ACL_SUCCESS) {
    return FAILED;
  }

  loc_comm_res::LocCommResInfo loc_comm_res_info{};
  loc_comm_res_info.version = kConfigVersion;

  Status ret = BuildNetInstanceId(device_id, loc_comm_res_info.net_instance_id);
  if (ret != SUCCESS) {
    return ret;
  }

  ret = BuildEndpointList(device_id, phy_device_id, loc_comm_res_info.endpoint_list);
  if (ret != SUCCESS) {
    return ret;
  }

  try {
    nlohmann::json j = loc_comm_res_info;
    loc_comm_res = j.dump();
  } catch (const nlohmann::json::exception &) {
    return FAILED;
  }

  return SUCCESS;
}

Status LocCommResGenerator::BuildNetInstanceId(int32_t device_id, std::string &net_instance_id) {
  std::string soc_name;
  Status ret = GetSocName(soc_name);
  if (ret != SUCCESS) {
    return ret;
  }

  if (IsA3Soc(soc_name)) {
    int64_t super_pod_id = 0;
    aclError acl_ret = aclrtGetDeviceInfo(static_cast<uint32_t>(device_id),
                                          ACL_DEV_ATTR_SUPER_POD_ID,
                                          &super_pod_id);
    if (acl_ret != ACL_SUCCESS) {
      return FAILED;
    }
    net_instance_id = std::to_string(super_pod_id);
    return SUCCESS;
  }

  if (IsA2Soc(soc_name)) {
    return GetHostIp(net_instance_id);
  }

  return PARAM_INVALID;
}

Status LocCommResGenerator::BuildEndpointList(
    int32_t device_id,
    int32_t phy_device_id,
    std::vector<loc_comm_res::EndpointInfo> &endpoint_list) {
  endpoint_list.clear();

  loc_comm_res::EndpointInfo roce_endpoint{};
  Status ret = BuildRoceEndpoint(phy_device_id, roce_endpoint);
  if (ret != SUCCESS) {
    return ret;
  }
  endpoint_list.emplace_back(roce_endpoint);

  loc_comm_res::EndpointInfo hccs_endpoint{};
  ret = BuildHccsEndpoint(phy_device_id, hccs_endpoint);
  if (ret != SUCCESS) {
    return ret;
  }
  endpoint_list.emplace_back(hccs_endpoint);

  (void)device_id;
  return SUCCESS;
}

Status LocCommResGenerator::BuildRoceEndpoint(
    int32_t phy_device_id,
    loc_comm_res::EndpointInfo &endpoint) {
  std::string device_ip;
  Status ret = GetDeviceIp(phy_device_id, device_ip);
  if (ret != SUCCESS) {
    return ret;
  }

  endpoint.protocol = kProtocolRoce;
  endpoint.comm_id = device_ip;
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status LocCommResGenerator::BuildHccsEndpoint(
    int32_t phy_device_id,
    loc_comm_res::EndpointInfo &endpoint) {
  endpoint.protocol = kProtocolHccs;
  endpoint.comm_id = std::to_string(phy_device_id);
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status LocCommResGenerator::GetHostIp(std::string &host_ip) {
  std::unique_ptr<FILE, int (*)(FILE *)> pipe(
      popen("hostname -I | awk '{print $1}' 2>&1", "r"), pclose);
  if (!pipe) {
    return FAILED;
  }

  std::array<char, kBufferMaxSize> buffer{};
  char *ret = fgets(buffer.data(), static_cast<int32_t>(buffer.size()), pipe.get());
  if (ret == nullptr) {
    return FAILED;
  }

  host_ip = buffer.data();
  while (!host_ip.empty() &&
         (host_ip.back() == '\n' || host_ip.back() == '\r' || host_ip.back() == ' ')) {
    host_ip.pop_back();
  }

  if (host_ip.empty()) {
    return FAILED;
  }

  return SUCCESS;
}

Status LocCommResGenerator::GetSocName(std::string &soc_name) {
  const char *soc_name_ptr = aclrtGetSocName();
  if (soc_name_ptr == nullptr) {
    return FAILED;
  }

  soc_name = soc_name_ptr;
  if (soc_name.empty()) {
    return FAILED;
  }

  return SUCCESS;
}

void LocCommResGenerator::ExtractIpAddress(const std::string &output_str, std::string &ip) {
  const std::string prefix = "ipaddr:";
  auto pos = output_str.find(prefix);
  if (pos != std::string::npos) {
    pos += prefix.length();
    auto end = output_str.find("\n", pos);
    ip = output_str.substr(pos, end - pos);
  }
}

Status LocCommResGenerator::GetHccnOutput(const std::string &command, std::string &result) {
  std::string command_with_stderr = command + " 2>&1";
  std::array<char, kBufferMaxSize> buffer{};
  std::unique_ptr<FILE, int (*)(FILE *)> pipe(
      popen(command_with_stderr.c_str(), "r"), pclose);
  if (!pipe) {
    return FAILED;
  }

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }

  return SUCCESS;
}

Status LocCommResGenerator::ExecuteCommandAndParseIp(
    const std::string &command,
    std::string &output,
    std::string &ip) {
  Status ret = GetHccnOutput(command, output);
  if (ret != SUCCESS) {
    return ret;
  }

  ExtractIpAddress(output, ip);
  return SUCCESS;
}

Status LocCommResGenerator::GetIpAddressFromHccnTool(uint32_t phy_device_id, std::string &ip) {
  std::string command;
  std::string output;
  constexpr const char *kHccnToolPath = "/usr/local/Ascend/driver/tools/hccn_tool";

  if (mmAccess(kHccnToolPath) == EN_OK) {
    command = std::string(kHccnToolPath) + " -i " + std::to_string(phy_device_id) + " -ip -g";
  } else {
    std::string check_cmd = "command -v hccn_tool > /dev/null 2>&1";
    if (system(check_cmd.c_str()) != 0) {
      return SUCCESS;
    }
    command = "hccn_tool -i " + std::to_string(phy_device_id) + " -ip -g";
  }

  Status ret = ExecuteCommandAndParseIp(command, output, ip);
  if (ret != SUCCESS) {
    return ret;
  }

  return SUCCESS;
}

Status LocCommResGenerator::GetDeviceIp(int32_t phy_device_id, std::string &device_ip) {
  constexpr const char *kFilePath = "/etc/hccn.conf";
  char resolved_path[MMPA_MAX_PATH] = {};
  auto mm_ret = mmRealPath(kFilePath, resolved_path, MMPA_MAX_PATH);

  if (mm_ret == EN_OK) {
    if (mmAccess(resolved_path) != EN_OK) {
      return FAILED;
    }

    std::ifstream file(resolved_path);
    if (!file.is_open()) {
      return FAILED;
    }

    std::string line;
    std::string target_key = "address_" + std::to_string(phy_device_id) + "=";
    constexpr size_t kValidItemNum = 2U;

    while (std::getline(file, line)) {
      if (line.find(target_key) != 0) {
        continue;
      }

      const auto address_val = Split(line, '=');
      if (address_val.size() != kValidItemNum) {
        return FAILED;
      }

      device_ip = address_val.back();
      if (CheckIp(device_ip) != SUCCESS) {
        return PARAM_INVALID;
      }
      return SUCCESS;
    }
  }

  std::string ip;
  Status ret = GetIpAddressFromHccnTool(static_cast<uint32_t>(phy_device_id), ip);
  if (ret != SUCCESS) {
    return ret;
  }

  if (!ip.empty()) {
    device_ip = ip;
    if (CheckIp(device_ip) != SUCCESS) {
      return PARAM_INVALID;
    }
    return SUCCESS;
  }

  HIXL_LOGE(FAILED, "Failed to get device ip from hccn.conf and hccn_tool, phy_device_id:%d", phy_device_id);
  return FAILED;
}

bool LocCommResGenerator::IsA2Soc(const std::string &soc_name) {
  return soc_name == kSocA2;
}

bool LocCommResGenerator::IsA3Soc(const std::string &soc_name) {
  return kV2Version.find(soc_name) != kV2Version.end();
}
}  // namespace hixl