/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_INC_EXTERNAL_HCCL_API_H_
#define CANN_HIXL_INC_EXTERNAL_HCCL_API_H_

#include <cstdint>
#include <netinet/in.h>
#include <string>
#include "hccl/hccl_types.h"
#include "../llm_datadist/hccl/hccl_mem_comm.h"
#ifdef __cplusplus
extern "C" {
#endif

using MemHandle = void *;
using FdHandle = void *;
using EndPointHandle = void *;
using ChannelHandle = uint64_t;

struct HixlBuf {
  void *addr;
  uint64_t len;
};

enum CommProtocol {
  COMM_PROTOCOL_RESERVED = -1,
  COMM_PROTOCOL_HCCS = 0,
  COMM_PROTOCOL_TCP = 1,
  COMM_PROTOCOL_ROCE = 2,
  COMM_PROTOCOL_UB_CTP = 3,
  COMM_PROTOCOL_UB_TP = 4
};

enum CommAddrType {
  COMM_ADDR_TYPE_RESERVED = -1,
  COMM_ADDR_TYPE_IP_V4 = 0,
  COMM_ADDR_TYPE_IP_V6 = 1,
  COMM_ADDR_TYPE_ID = 2
};

struct CommAddr {
  CommAddrType type;
  union {
    uint32_t id;
    struct in_addr addr;
    struct in6_addr addr6;
  };
};

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

inline bool operator == (const EndPointInfo& lhs, const EndPointInfo& rhs) {
  if (lhs.protocol != rhs.protocol) {
    return false;
  }

  if (lhs.protocol == COMM_PROTOCOL_UB_CTP ||
      lhs.protocol == COMM_PROTOCOL_UB_TP ||
      lhs.protocol == COMM_PROTOCOL_HCCS) {
    return lhs.addr.id == rhs.addr.id;
  }
  return true;
}

enum CommEngine {
  COMM_ENGINE_RESERVED = -1,
  COMM_ENGINE_HOSTCPU = 0
  };

struct HccsAttr {
};

struct RoCEAttr {
  uint32_t queueNum;
  uint32_t queueMode;
  uint16_t *udpSport;
  uint8_t tc;
  uint8_t sl;
  uint32_t retryCnt;
  uint32_t retryInterval;
};

struct JettyAttr {
  uint32_t mode;
};

struct UbAttr {
  JettyAttr *jettyAttr;
  uint32_t jettyNum;
};

struct HcommChannelDescNew {
  EndPointInfo remoteEndPoint;
  uint32_t notifyNum;
  union {
    HccsAttr hccsAttr;
    RoCEAttr roceAttr;
    UbAttr ubAttr;
  };
};

struct HcommBuf {
  void *addr;
  uint64_t len;
};

HcclResult HcommEndPointCreate(const EndPointInfo *endpoint, void **handle);

HcclResult HcommEndPointDestroy(void *handle);

HcclResult HcommMemReg(void *handle, HcclMem mem, void **mem_handle);

HcclResult HcommMemUnreg(void *handle, void *mem_handle);

HcclResult HcommMemExport(void *handle, const void *mem_handle, void **mem_desc, uint32_t *desc_len);

HcclResult HcommEndPointCreate(const EndPointInfo *end_point, void **end_point_handle);

HcclResult HcommEndPointDestroy(void *end_point_handle);

HcclResult HcommChannelCreate(void **end_point_handle, CommEngine engine, HcommChannelDescNew *channel_desc_list,
                              uint32_t list_num, const void **mem_handle_list, uint32_t mem_handle_list_num,
                              ChannelHandle *channel_list);

HcclResult HcommChannelDestroy(const ChannelHandle *channel_list, uint32_t list_num);

#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_INC_EXTERNAL_HCCL_API_H_
