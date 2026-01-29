/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_server.h"
#include "nlohmann/json.hpp"
#include "cs/hixl_cs_server.h"
#include "common/hixl_checker.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/hixl_inner_types.h"
#include "common/hixl_utils.h"

namespace hixl {

Status HixlServer::Initialize(const std::string &ip, int32_t port,
                              const std::vector<EndPointConfig> &data_endpoint_config_list) {
  data_endpoint_config_list_ = data_endpoint_config_list;
  std::vector<EndpointDesc> data_end_point_list;
  int32_t devLogicId = 0;
  int32_t devPhyId = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&devLogicId));
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(devLogicId, &devPhyId));
  for (const auto &it : data_endpoint_config_list) {
    EndpointDesc end_point_info{};
    HIXL_CHK_STATUS_RET(ConvertToEndPointInfo(it, end_point_info, static_cast<uint32_t>(devPhyId)));
    HIXL_LOGI("[ZC] end_point_info devPhyId: %u", end_point_info.loc.device.devPhyId);
    data_end_point_list.emplace_back(end_point_info);
  }
  const EndpointDesc *endpoints = data_end_point_list.data();
  HixlServerConfig config{};
  HIXL_CHK_STATUS_RET(HixlCSServerCreate(ip.c_str(), static_cast<uint32_t>(port), endpoints,
                      static_cast<uint32_t>(data_endpoint_config_list.size()),
                      &config, &server_handle_));
  //port > 0 初始化hixl server，否则作为hixl client注册内存用
  if (port > 0) {
    //注册回调函数且监听端口
    MsgProcessor send_endpoint_cb = [this](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
      (void)msg;
      (void)msg_len;
      std::string msg_str;
      HIXL_CHK_STATUS_RET(SerializeEndPointConfigList(data_endpoint_config_list_, msg_str),
                          "Failed to serialize endpoint config.");
      CtrlMsgHeader header{};
      header.magic = kMagicNumber;
      header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + msg_str.size());
      CtrlMsgType msg_type = CtrlMsgType::kGetEndPointInfoResp;
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &header, static_cast<uint64_t>(sizeof(header))));
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))));
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, msg_str.c_str(), static_cast<uint64_t>(msg_str.size())));
      return SUCCESS;
    };
    HIXL_CHK_STATUS_RET(HixlCSServerRegProc(server_handle_, CtrlMsgType::kGetEndPointInfoReq, send_endpoint_cb),
                        "Failed to register send endpoint info processor.");
    HIXL_CHK_STATUS_RET(HixlCSServerListen(server_handle_, static_cast<uint32_t>(port)),
                        "HixlServer listen failed, port:%d.", port);
  }
  return SUCCESS;
}

Status HixlServer::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHECK_NOTNULL(server_handle_);
  AddrInfo cur_info{};
  cur_info.start_addr = mem.addr;
  cur_info.end_addr = mem.addr + mem.len;
  cur_info.mem_type = type;
  std::lock_guard<std::mutex> lk(mtx_);
  for (const auto &it : handle_to_addr_) {
    const AddrInfo &info = it.second;
    // 检查地址范围是否重叠且内存类型相同
    if (!((cur_info.end_addr <= info.start_addr) || (cur_info.start_addr >= info.end_addr))
        && (cur_info.mem_type == info.mem_type)) {
      if (info.start_addr == cur_info.start_addr && info.end_addr == cur_info.end_addr) {
        // 完全相同的内存区域，可以重复注册
        mem_handle = it.first;
        HIXL_LOGI("Memory already registered, returning existing handle:%p", mem_handle);
        return SUCCESS;
      }
      HIXL_LOGE(PARAM_INVALID, "Mem addr range overlap with existing registered mem, "
                "new mem range:[0x%lx, 0x%lx), existing mem range:[0x%lx, 0x%lx).",
                cur_info.start_addr, cur_info.end_addr, info.start_addr, info.end_addr);
      return PARAM_INVALID;
    }
  }
  HcommMem hccl_mem{};
  hccl_mem.type = (type == MemType::MEM_DEVICE) ? HCCL_MEM_TYPE_DEVICE : HCCL_MEM_TYPE_HOST;
  hccl_mem.addr = reinterpret_cast<void *>(mem.addr);
  hccl_mem.size = mem.len;
  HIXL_CHK_STATUS_RET(HixlCSServerRegMem(server_handle_, nullptr, &hccl_mem, &mem_handle),
                      "Failed to register mem, addr:0x%lx, size:%lu, type:%d.",
                      mem.addr, mem.len, static_cast<int32_t>(type));
  handle_to_addr_[mem_handle] = cur_info;
  return SUCCESS;
}

Status HixlServer::DeregisterMem(MemHandle &mem_handle) {
  HIXL_CHECK_NOTNULL(server_handle_);
  // 判断mem_handle是否存在
  std::lock_guard<std::mutex> lk(mtx_);
  auto it = handle_to_addr_.find(mem_handle);
  if (it == handle_to_addr_.end()) {
    HIXL_LOGW("mem_handle:%p is not registered.", mem_handle);
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(HixlCSServerUnregMem(server_handle_, mem_handle), "Failed to deregister mem, handle:%p.",
                      mem_handle);
  handle_to_addr_.erase(it);
  mem_handle = nullptr;
  return SUCCESS;
}

Status HixlServer::Finalize() {
  if (server_handle_ == nullptr) {
    return SUCCESS;
  }
  // 注销所有注册的内存
  std::vector<MemHandle> handles_to_deregister;
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto mem_handle : handles_to_deregister) {
    Status ret = HixlCSServerUnregMem(server_handle_, mem_handle);
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "Failed to deregister mem, handle:%p.", mem_handle);
    }
  }
  handle_to_addr_.clear();
  HIXL_CHK_STATUS_RET(HixlCSServerDestroy(server_handle_), "Failed to destroy hixl server.");
  server_handle_ = nullptr;
  return SUCCESS;
}

} // namespace hixl
