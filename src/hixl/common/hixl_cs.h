/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_CS_H_
#define CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_CS_H_

#include <cstdint>
#include <netinet/in.h>
#include <string>
#include "hccl_api.h"

#ifdef __cplusplus
extern "C" {
#endif

using HixlServerHandle = void *;
using HixlClientHandle = void *;
using HixlStatus = uint32_t;

constexpr HixlStatus HIXL_SUCCESS = 0U;
constexpr HixlStatus HIXL_PARAM_INVALID = 103900U;
constexpr HixlStatus HIXL_TIMEOUT = 103901U;
constexpr HixlStatus HIXL_FAILED = 503900U;

struct HixlServerConfig {
  uint8_t reserved[128] = {};
};

/**
 * @brief 创建Server
 * @param [in] ip server的host侧ip地址
 * @param [in] port server的host侧ip监听的端口
 * @param [in] endpoint_list server的Endpoint资源列表
 * @param [in] list_num server的Endpoint资源列表数量
 * @param [out] server_handle server创建返回的handle信息，用于后续调用其他接口
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerCreate(const char *ip, uint32_t port, const EndPointInfo *endpoint_list, uint32_t list_num,
                              const HixlServerConfig *config, HixlServerHandle *server_handle);

/**
 * @brief Server注册内存
 * @param [in] server_handle 创建server返回的handle
 * @param [in] mem_tag 用于表标识注册内存的描述信息
 * @param [in] mem server的注册的内存信息
 * @param [out] mem_handle server注册内存返回的handle信息
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerRegMem(HixlServerHandle server_handle, const char *mem_tag, const HcclMem *mem,
                              MemHandle *mem_handle);

/**
 * @brief Server启动监听
 * @param [in] server_handle 创建server返回的handle
 * @param [in] backlog server连接队列的最大长度
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerListen(HixlServerHandle server_handle, uint32_t backlog);

/**
 * @brief Server解注册内存
 * @param [in] server_handle 创建server返回的handle
 * @param [in] mem_handle server注册内存返回的handle信息
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerUnregMem(HixlServerHandle server_handle, MemHandle mem_handle);

/**
 * @brief Server销毁
 * @param [in] server_handle 创建server返回的handle
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSServerDestroy(HixlServerHandle server_handle);

/**
 * @brief 创建 Client 实例
 * @param [in] server_ip 服务端的监听ip地址
 * @param [in] server_port 服务端的监听端口号
 * @param [in] src_endpoint 源端点
 * @param [in] dst_endpoint 目标端点
 * @param [out] client_handle 输出的客户端句柄
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientCreate(const char *server_ip, uint32_t server_port, const EndPointInfo *src_endpoint,
                              const EndPointInfo *dst_endpoint, HixlClientHandle *client_handle);

/**
 * @brief 发起 Client 连接（同步建链入口）
 * @param [in] client_handle 客户端句柄
 * @param [in] timeout_ms   连接超时时间（ms）
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientConnectSync(HixlClientHandle client_handle, uint32_t timeout_ms);

/**
 * @brief 获取服务端共享内存信息
 * @param [in] client_handle 客户端句柄
 * @param [out] remote_mem_list 输出的服务端共享内存列表
 * @param [out] mem_tag_list 输出的服务端共享内存标识列表
 * @param [out] list_num 输出的共享内存实际数量
 * @param [in] timeout_ms 请求的超时时间（ms）
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientGetRemoteMem(HixlClientHandle client_handle, HcclMem **remote_mem_list, char ***mem_tag_list,
                                    uint32_t *list_num, uint32_t timeout_ms);

/**
 * @brief 销毁 Client 实例
 * @param [in] client_handle 客户端句柄
 * @return 成功:SUCCESS, 失败:其它.
 */
HixlStatus HixlCSClientDestroy(HixlClientHandle client_handle);

#ifdef __cplusplus
}
#endif

#endif  // CANN_HIXL_INC_EXTERNAL_HIXL_HIXL_CS_H_
