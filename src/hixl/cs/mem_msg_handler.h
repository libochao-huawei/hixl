/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_COMMON_HIXL_GET_REMOTE_MEM_MSG_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_COMMON_HIXL_GET_REMOTE_MEM_MSG_HANDLER_H_

#include <cstdint>
#include <vector>
#include <string>

#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_utils.h"
#include "common/hixl_log.h"
#include "common/ctrl_msg.h"
#include "endpoint.h"

namespace hixl {

/**
 * GetRemoteMem 消息编解码（client 侧）
 *
 * 协议约定（**header.body_size 一定包含 CtrlMsgType 在内**）：
 *
 *  GetRemoteMemReq（client -> server）:
 *    header.magic     = kMagicNumber
 *    header.body_size = sizeof(CtrlMsgType) + sizeof(GetRemoteMemReq)
 *    body             = [CtrlMsgType::kGetRemoteMemReq][GetRemoteMemReq]
 *
 *  GetRemoteMemResp（server -> client）:
 *    header.magic     = kMagicNumber
 *    header.body_size = sizeof(CtrlMsgType) + json_len
 *    body             = [CtrlMsgType::kGetRemoteMemResp][json_bytes...]
 *
 *  json 结构（与 server 侧 Serialize 对应）：
 *
 *  {
 *    "result": <Status>,                // uint32，对应 Status 枚举
 *    "mem_descs": [
 *      {
 *        "tag": "xxx",                  // 内存标识字符串
 *        "export_desc": "<memDesc 原始二进制转成的 string>",
 *        "mem": {                       // server 端 HcclMem 描述
 *          "type": <uint32>,            // HixlMemType
 *          "addr": <uint64>,            // server 侧虚拟地址，作为范围校验用
 *          "size": <uint64>             // server 侧内存大小
 *        }
 *      },
 *      ...
 *    ]
 *  }
 *
 *  其中：
 *    - client 侧 RecvGetRemoteMemResponse 负责解析上述 JSON，构造出 std::vector<HixlMemDesc>：
 *        - HixlMemDesc::tag        <- "tag"
 *        - HixlMemDesc::export_desc/export_len <- "export_desc"
 *        - HixlMemDesc::mem        <- "mem"（server 侧 HcclMem 信息）
 *    - 后续 HixlCsClient::ImportRemoteMem 使用 export_desc 调用 Endpoint::MemImport，
 *      得到本端可访问的 addr/len，填充给上层使用的 HixlMem，并调用 HixlMemStore::RecordMemory(true, ...)
 *      记录 server 侧内存区域。
 */

class MemMsgHandler {
 public:
  static Status SendGetRemoteMemRequest(int32_t socket, uint64_t endpoint_handle, uint32_t timeout_ms = 0U);

  static Status RecvGetRemoteMemResponse(int32_t socket, std::vector<HixlMemDesc> &mem_descs,
                                         uint32_t timeout_ms = 0U);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_HIXL_GET_REMOTE_MEM_MSG_HANDLER_H_
