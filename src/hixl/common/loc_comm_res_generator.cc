/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "loc_comm_res_generator.h"
#include <cstdlib>
#include <string>
#include "acl/acl.h"
#include "nlohmann/json.hpp"
#include "common/hixl_utils.h"

namespace hixl {
namespace {
constexpr const char kConfigVersion[] = "1.3";
bool IsIntraRoceEnabled() {
  const char *env_ret = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  if (env_ret == nullptr) {
    return false;
  }
  return std::string(env_ret) == "1";
}
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

Status LocCommResGenerator::GenerateInfo(int32_t device_id, const std::string &local_engine,
                                         loc_comm_res::LocCommResInfo &loc_comm_res_info) {
  int32_t phy_device_id = 0;
  aclError acl_ret = aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id);
  if (acl_ret != ACL_SUCCESS) {
    return FAILED;
  }

  loc_comm_res_info = {};
  loc_comm_res_info.version = kConfigVersion;

  Status ret = BuildNetInstanceId(device_id, local_engine, loc_comm_res_info.net_instance_id);
  if (ret != SUCCESS) {
    return ret;
  }

  ret = BuildEndpointList(phy_device_id, loc_comm_res_info.endpoint_list);
  if (ret != SUCCESS) {
    return ret;
  }

  return SUCCESS;
}

Status LocCommResGenerator::Generate(int32_t device_id, const std::string &local_engine, std::string &loc_comm_res) {
  loc_comm_res::LocCommResInfo loc_comm_res_info{};
  Status ret = GenerateInfo(device_id, local_engine, loc_comm_res_info);
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

Status LocCommResGenerator::BuildNetInstanceId(int32_t device_id, const std::string &local_engine,
                                               std::string &net_instance_id) {
  SocType soc_type = SocType::kOther;
  Status ret = GetSocType(soc_type);
  if (ret != SUCCESS) {
    return ret;
  }

  if (soc_type == SocType::kA3) {
    int64_t super_pod_id = 0;
    aclError acl_ret = aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id);
    if (acl_ret != ACL_SUCCESS) {
      return FAILED;
    }
    net_instance_id = std::to_string(super_pod_id);
    return SUCCESS;
  }

  if (soc_type == SocType::kA2) {
    return GetHostIpFromLocalEngine(local_engine, net_instance_id);
  }

  return PARAM_INVALID;
}

Status LocCommResGenerator::BuildEndpointList(int32_t phy_device_id,
                                              std::vector<loc_comm_res::EndpointInfo> &endpoint_list) {
  endpoint_list.clear();

  loc_comm_res::EndpointInfo roce_endpoint{};
  Status ret = BuildRoceEndpoint(phy_device_id, roce_endpoint);
  if (ret != SUCCESS) {
    return ret;
  }
  endpoint_list.emplace_back(roce_endpoint);

  if (IsIntraRoceEnabled()) {
    HIXL_LOGI("HCCL_INTRA_ROCE_ENABLE=1, only generate ROCE endpoint");
    return SUCCESS;
  }

  loc_comm_res::EndpointInfo hccs_endpoint{};
  ret = BuildHccsEndpoint(phy_device_id, hccs_endpoint);
  if (ret != SUCCESS) {
    return ret;
  }
  endpoint_list.emplace_back(hccs_endpoint);

  return SUCCESS;
}

Status LocCommResGenerator::BuildRoceEndpoint(int32_t phy_device_id, loc_comm_res::EndpointInfo &endpoint) {
  std::string device_ip;
  Status ret = GetDeviceIp(phy_device_id, device_ip);
  if (ret != SUCCESS) {
    return ret;
  }
  if (device_ip.empty()) {
    HIXL_LOGE(FAILED, "Failed to get device ip, phy_device_id:%d", phy_device_id);
    return FAILED;
  }

  endpoint.protocol = kProtocolRoce;
  endpoint.comm_id = device_ip;
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status LocCommResGenerator::BuildHccsEndpoint(int32_t phy_device_id, loc_comm_res::EndpointInfo &endpoint) {
  endpoint.protocol = kProtocolHccs;
  endpoint.comm_id = std::to_string(phy_device_id);
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status LocCommResGenerator::GetHostIpFromLocalEngine(const std::string &local_engine, std::string &host_ip) {
  int32_t host_port = 0;
  Status ret = ParseListenInfo(local_engine, host_ip, host_port);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "Failed to parse host ip from local_engine:%s", local_engine.c_str());
    return ret;
  }

  if (host_ip.empty()) {
    return FAILED;
  }

  if (host_ip == "0.0.0.0" || host_ip == "::") {
    HIXL_LOGE(PARAM_INVALID, "Wildcard ip is not allowed for auto-generated net_instance_id, local_engine:%s",
              local_engine.c_str());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status LocCommResGenerator::GetDeviceIp(int32_t phy_device_id, std::string &device_ip) {
  Status ret = hixl::GetDeviceIp(phy_device_id, device_ip);
  if (ret != SUCCESS) {
    return ret;
  }
  if (device_ip.empty()) {
    HIXL_LOGE(FAILED, "Failed to get device ip from hccn.conf and hccn_tool, phy_device_id:%d", phy_device_id);
    return FAILED;
  }
  return SUCCESS;
}
}  // namespace hixl
