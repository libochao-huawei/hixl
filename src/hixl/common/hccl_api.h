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
#include "hixl/hixl_types.h"
#ifdef __cplusplus
extern "C" {
#endif

using MemHandle = void *;
using FdHandle = void *;
using EndPointHandle = void *;
using ChannelHandle = uint64_t;
using ThreadHandle = uint64_t;

// typedef enum {
//   HCCL_MEM_TYPE_DEVICE,  // device
//   HCCL_MEM_TYPE_HOST,    // host
//   HCCL_MEM_TYPE_NUM
// } HcclMemType;

typedef struct {
  HcclMemType type;
  void *addr;
  uint64_t size;
} HcommMem;

struct HixlBuf {
  void *addr;
  uint64_t len;
};

enum CommProtocol {
  COMM_PROTOCOL_RESERVED = -1,
  COMM_PROTOCOL_HCCS = 0,
  COMM_PROTOCOL_ROCE = 1,
  COMM_PROTOCOL_PCIE= 2,
  COMM_PROTOCOL_SIO = 3,
  COMM_PROTOCOL_UBC_CTP = 4,
  COMM_PROTOCOL_UBC_TP = 5,
  COMM_PROTOCOL_UB_MEM = 6
};

enum CommAddrType {
  COMM_ADDR_TYPE_RESERVED = -1,
  COMM_ADDR_TYPE_IP_V4 = 0,
  COMM_ADDR_TYPE_IP_V6 = 1,
  COMM_ADDR_TYPE_ID = 2,
  COMM_ADDR_TYPE_EID = 3
};

constexpr uint32_t COMM_ADDR_EID_LEN = 16;
struct CommAddr {
  CommAddrType type;
  union {
    uint32_t id;
    uint8_t eid[COMM_ADDR_EID_LEN];
    struct in_addr addr;
    struct in6_addr addr6;
  };
};

enum EndpointLocType {
  ENDPOINT_LOC_TYPE_RESERVED = -1,
  ENDPOINT_LOC_TYPE_HOST = 0,
  ENDPOINT_LOC_TYPE_DEVICE = 1,
};

struct EndpointLoc {
  EndpointLocType locType;
  union {
    u_int8_t raws[60];
    struct {
      uint32_t devPhyId;
      uint32_t superDevId;
      uint32_t serverIdx;
      uint32_t superPodIdx;
    } device;
    struct {
      uint32_t id;
    } host;
  };
};

struct EndpointDesc {
  EndpointLoc loc;
  CommProtocol protocol;
  CommAddr commAddr;
  union {
    u_int8_t raw[52];
  };
};

enum CommEngine {
  COMM_ENGINE_RESERVED = -1,
  COMM_ENGINE_CPU = 1,
  COMM_ENGINE_CPU_TS = 2,
  COMM_ENGINE_AICPU = 3,
  COMM_ENGINE_AICPU_TS = 4,
  COMM_ENGINE_AIV = 5,
  COMM_ENGINE_CCU = 6
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
using HcommSocket = void *;
struct HcommChannelDesc {
  EndpointDesc remoteEndpoint;
  uint32_t notifyNum;

  bool exchangeAllMems;
  void **memHandles;
  uint32_t memHandleNum;
  union {
    uint8_t raws[128];
    struct {
      uint32_t queueNum;
      uint32_t retryCnt;
      uint32_t retryInterval;
      uint32_t tc;
      uint32_t sl;
    } roceAttr;

  };
  HcommSocket socket;
  uint16_t port;
};

  // struct HcommChannelDesc {
  //   EndpointDesc remoteEndPoint;
  //   uint32_t notifyNum;
  //
  //   bool exchangeAllMems;
  //   void **memHandles;
  //   uint32_t memHandleNum;
  //   union {
  //     HccsAttr hccsAttr;
  //     RoCEAttr roceAttr;
  //     UbAttr ubAttr;
  //   };
  // };

struct HcommBuf {
  void *addr;
  uint64_t len;
};

HcclResult HcommEndpointCreate(const EndpointDesc *endpoint, void **handle);

HcclResult HcommEndpointDestroy(void *handle);

HcclResult HcommMemReg(void *handle, HcommMem mem, void **mem_handle);

HcclResult HcommMemUnreg(void *handle, void *mem_handle);

HcclResult HcommMemExport(void *handle, const void *mem_handle, void **mem_desc, uint32_t *desc_len);

HcclResult HcommMemImport(void *end_point_handle, const void *mem_desc, uint32_t desc_len, HcommBuf *out_buf);

HcclResult HcommMemUnimport(void *endPointHandle, const HcommBuf *buf);

// HcclResult HcommChannelCreate(void **end_point_handle, CommEngine engine, HcommChannelDesc *channel_desc_list,
//                               uint32_t list_num, const void **mem_handle_list, uint32_t mem_handle_list_num,
//                               ChannelHandle *channel_list);

HcclResult HcommChannelCreate(void **end_point_handle, CommEngine engine, HcommChannelDesc *channel_desc_list,
                              uint32_t list_num, ChannelHandle *channel_list);

HcclResult HcommChannelDestroy(const ChannelHandle *channel_list, uint32_t list_num);

HcclResult HcommChannelGetStatus(const ChannelHandle *channel_list, uint32_t list_num, int32_t *status_list);

void HcommReadNbi(ChannelHandle channel, void *dst, void *src, uint64_t len);

void HcommWriteNbi(ChannelHandle channel, void *dst, void *src, uint64_t len);

int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);

int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);

void HcommChannelFence(ChannelHandle channel);

HcclResult HcommThreadAlloc(CommEngine engine, uint32_t threadNum, uint32_t notifyNumPerThread, ThreadHandle *thread);

HcclResult HcommThreadFree(ThreadHandle thread);
#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_INC_EXTERNAL_HCCL_API_H_
