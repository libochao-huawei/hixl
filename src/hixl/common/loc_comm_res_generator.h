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

#ifndef CANN_HIXL_SRC_HIXL_COMMON_LOC_COMM_RES_GENERATOR_H_
#define CANN_HIXL_SRC_HIXL_COMMON_LOC_COMM_RES_GENERATOR_H_

#include <string>
#include <vector>

#include "hixl/hixl_types.h"

namespace hixl {
namespace loc_comm_res {
struct EndpointInfo {
  std::string protocol;
  std::string comm_id;
  std::string placement;
};

struct LocCommResInfo {
  std::string version;
  std::string net_instance_id;
  std::vector<EndpointInfo> endpoint_list;
};
}  // namespace loc_comm_res

class LocCommResGenerator {
 public:
  static Status Generate(int32_t device_id, std::string &loc_comm_res);
  static Status GetDeviceIp(int32_t phy_device_id, std::string &device_ip);

 private:
  static Status BuildNetInstanceId(int32_t device_id, std::string &net_instance_id);
  static Status BuildEndpointList(int32_t device_id,
                                  int32_t phy_device_id,
                                  std::vector<loc_comm_res::EndpointInfo> &endpoint_list);
  static Status BuildRoceEndpoint(int32_t phy_device_id,
                                  loc_comm_res::EndpointInfo &endpoint);
  static Status BuildHccsEndpoint(int32_t phy_device_id,
                                  loc_comm_res::EndpointInfo &endpoint);
  static Status GetHostIp(std::string &host_ip);
  static Status GetSocName(std::string &soc_name);
  static void ExtractIpAddress(const std::string &output_str, std::string &ip);
  static Status GetHccnOutput(const std::string &command, std::string &result);
  static Status ExecuteCommandAndParseIp(const std::string &command,
                                         std::string &output,
                                         std::string &ip);
  static Status GetIpAddressFromHccnTool(uint32_t phy_device_id, std::string &ip);
  static bool IsA2Soc(const std::string &soc_name);
  static bool IsA3Soc(const std::string &soc_name);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_LOC_COMM_RES_GENERATOR_H_