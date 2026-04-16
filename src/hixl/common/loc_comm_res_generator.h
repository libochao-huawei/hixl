/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
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
  static Status GenerateInfo(int32_t device_id, const std::string &local_engine,
                             loc_comm_res::LocCommResInfo &loc_comm_res_info);
  static Status GetDeviceIp(int32_t phy_device_id, std::string &device_ip);

 private:
  static Status BuildNetInstanceId(int32_t device_id,
                                   const std::string &local_engine,
                                   std::string &net_instance_id);
  static Status BuildEndpointList(int32_t phy_device_id,
                                  std::vector<loc_comm_res::EndpointInfo> &endpoint_list);
  static Status BuildRoceEndpoint(int32_t phy_device_id, loc_comm_res::EndpointInfo &endpoint);
  static Status BuildHccsEndpoint(int32_t phy_device_id, loc_comm_res::EndpointInfo &endpoint);
  static Status GetHostIpFromLocalEngine(const std::string &local_engine, std::string &host_ip);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_LOC_COMM_RES_GENERATOR_H_
