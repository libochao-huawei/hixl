/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_HIXL_HCCL_API_H_
#define CANN_HIXL_SRC_HIXL_COMMON_HIXL_HCCL_API_H_

#include "hccl/hccl_mem_comm.h"
#ifdef __cplusplus
extern "C" {
#endif

enum EndPointLocation {
  END_POINT_LOCATION_RESERVED = -1,
  END_POINT_LOCATION_HOST = 0,
  END_POINT_LOCATION_DEVICE = 1,
};

struct EndPointInfo {
  EndPointLocation location;
  CommProtocol protocol;
  CommAddr addr;
};

inline bool operator==(const EndPointInfo &lhs, const EndPointInfo &rhs) {
  if (lhs.protocol != rhs.protocol) {
    return false;
  }

  if (lhs.protocol == COMM_PROTOCOL_HCCS) {
    return lhs.addr.id == rhs.addr.id;
  }
  return true;
}
#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_HCCL_API_H_