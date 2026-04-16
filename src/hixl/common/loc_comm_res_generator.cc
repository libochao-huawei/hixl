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
#include "common/hixl_checker.h"
#include "common/hixl_utils.h"

namespace hixl {
namespace {
constexpr const char kConfigVersion[] = "1.3";
bool IsIntraRoceEnabled() {
  const char *env_ret = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(env_ret == nullptr, false, "HCCL_INTRA_ROCE_ENABLE is not set");
  return std::string(env_ret) == "1";
}
}  // namespace

Status LocCommResGenerator::GenerateInfo(int32_t device_id, const std::string &local_engine,
                                         loc_comm_res::LocCommResInfo &loc_comm_res_info) {
  int32_t phy_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id),
                   "device_id:%d, failed to get physical device id", device_id);

  loc_comm_res_info = {};
  loc_comm_res_info.version = kConfigVersion;

  HIXL_CHK_STATUS_RET(BuildNetInstanceId(device_id, local_engine, loc_comm_res_info.net_instance_id),
                      "BuildNetInstanceId failed, device_id:%d, local_engine:%s", device_id, local_engine.c_str());
  HIXL_CHK_STATUS_RET(BuildEndpointList(phy_device_id, loc_comm_res_info.endpoint_list),
                      "BuildEndpointList failed, phy_device_id:%d", phy_device_id);

  return SUCCESS;
}

Status LocCommResGenerator::BuildNetInstanceId(int32_t device_id, const std::string &local_engine,
                                               std::string &net_instance_id) {
  SocType soc_type = SocType::kOther;
  HIXL_CHK_STATUS_RET(GetSocType(soc_type), "GetSocType failed");

  if (soc_type == SocType::kV3) {
    int64_t super_pod_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id),
                     "device_id:%d, failed to get super pod id", device_id);
    net_instance_id = std::to_string(super_pod_id);
    return SUCCESS;
  }

  HIXL_CHK_BOOL_RET_STATUS(soc_type == SocType::kV2, PARAM_INVALID,
                           "Unsupported soc_type:%d for auto-generated net_instance_id",
                           static_cast<int32_t>(soc_type));

  return GetHostIpFromLocalEngine(local_engine, net_instance_id);
}

Status LocCommResGenerator::BuildEndpointList(int32_t phy_device_id,
                                              std::vector<loc_comm_res::EndpointInfo> &endpoint_list) {
  endpoint_list.clear();

  loc_comm_res::EndpointInfo roce_endpoint{};
  HIXL_CHK_STATUS_RET(BuildRoceEndpoint(phy_device_id, roce_endpoint), "BuildRoceEndpoint failed, phy_device_id:%d",
                      phy_device_id);
  endpoint_list.emplace_back(roce_endpoint);

  if (IsIntraRoceEnabled()) {
    HIXL_LOGI("HCCL_INTRA_ROCE_ENABLE=1, only generate ROCE endpoint");
    return SUCCESS;
  }

  loc_comm_res::EndpointInfo hccs_endpoint{};
  HIXL_CHK_STATUS_RET(BuildHccsEndpoint(phy_device_id, hccs_endpoint), "BuildHccsEndpoint failed, phy_device_id:%d",
                      phy_device_id);
  endpoint_list.emplace_back(hccs_endpoint);

  return SUCCESS;
}

Status LocCommResGenerator::BuildRoceEndpoint(int32_t phy_device_id, loc_comm_res::EndpointInfo &endpoint) {
  std::string device_ip;
  HIXL_CHK_STATUS_RET(GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d", phy_device_id);
  HIXL_CHK_BOOL_RET_STATUS(!device_ip.empty(), FAILED, "Failed to get device ip, phy_device_id:%d", phy_device_id);

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
  HIXL_CHK_STATUS_RET(ParseListenInfo(local_engine, host_ip, host_port), "Failed to parse host ip from local_engine:%s",
                      local_engine.c_str());
  HIXL_CHK_BOOL_RET_STATUS(!host_ip.empty(), FAILED, "Failed to get host ip from local_engine:%s",
                           local_engine.c_str());
  return SUCCESS;
}

Status LocCommResGenerator::GetDeviceIp(int32_t phy_device_id, std::string &device_ip) {
  HIXL_CHK_STATUS_RET(hixl::GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d",
                      phy_device_id);
  HIXL_CHK_BOOL_RET_STATUS(!device_ip.empty(), FAILED,
                           "Failed to get device ip from hccn.conf and hccn_tool, phy_device_id:%d", phy_device_id);
  return SUCCESS;
}
}  // namespace hixl
