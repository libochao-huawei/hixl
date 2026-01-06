/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_cs_server.h"
#include <sys/epoll.h>
#include "nlohmann/json.hpp"
#include "common/hixl_checker.h"
#include "common/ctrl_msg.h"
#include "common/scope_guard.h"
#include "common/ctrl_msg_plugin.h"

static inline void to_json(nlohmann::json &j, const HcclMem &m) {
  j = nlohmann::json{};
  j["type"] = m.type;
  j["addr"] = static_cast<uint64_t>(reinterpret_cast<intptr_t>(m.addr));
  j["size"] = m.size;
}

namespace hixl {
namespace {
constexpr int32_t kMaxEventsNum = 128;  // epoll_wait并发处理事件数量，减少epoll系统调用
constexpr int32_t kEpollWaitTimeInMillis = 100;  // epoll_wait等待超时时间
constexpr const char *kTransFinishedFlagName = "_hixl_builtin_dev_trans_flag";  // client用于感知收发完成的标识
}  // namespace

Status HixlCSServer::InitTransFinishedFlag() {
  HIXL_CHK_RT_RET(
      rtMalloc(&trans_flag_, sizeof(int64_t), RT_MEMORY_HBM | RT_MEMORY_POLICY_HUGE_PAGE_ONLY, HIXL_MODULE_NAME));
  int64_t trans_finished_flag = 1;
  HIXL_CHK_RT_RET(
      rtMemcpy(trans_flag_, sizeof(int64_t), &trans_finished_flag, sizeof(int64_t), RT_MEMCPY_HOST_TO_DEVICE));
  HcclMem mem{};
  mem.type = HCCL_MEM_TYPE_DEVICE;
  mem.addr = trans_flag_;
  mem.size = sizeof(int64_t);
  HIXL_CHK_STATUS_RET(RegisterMem(kTransFinishedFlagName, &mem, &trans_flag_handle_),
                      "Failed to reg trans finished flag");
  return SUCCESS;
}

Status HixlCSServer::Initialize(const EndPointInfo *endpoint_list, uint32_t list_num, const HixlServerConfig *config) {
  HIXL_CHECK_NOTNULL(endpoint_list);
  HIXL_CHECK_NOTNULL(config);
  HIXL_CHK_BOOL_RET_STATUS(list_num > 0, PARAM_INVALID, "endpoint list num:%u is invalid, must > 0", list_num);
  for (uint32_t i = 0U; i < list_num; ++i) {
    EndPointHandle handle = nullptr;
    HIXL_CHK_STATUS_RET(endpoint_store_.CreateEndpoint(endpoint_list[i], handle), "Failed to create endpoint.");
  }
  msg_handler_.RegisterMsgProcessor(CtrlMsgType::kCreateChannelReq,
                                    [this](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
                                      return this->CreateChannel(fd, msg, msg_len);
                                    });
  msg_handler_.RegisterMsgProcessor(CtrlMsgType::kGetRemoteMemReq,
                                    [this](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
                                      return this->GetRemoteMem(fd, msg, msg_len);
                                    });
  msg_handler_.RegisterMsgProcessor(CtrlMsgType::kDestroyChannelReq,
                                    [this](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
                                      return this->DestroyChannel(fd, msg, msg_len);
                                    });
  CtrlMsgPlugin::Initialize();
  msg_handler_.Initialize();
  HIXL_CHK_STATUS_RET(InitTransFinishedFlag(), "Failed to init trans finished flag");
  HIXL_EVENT("[HixlServer] init success, endpoint_list_num:%u", list_num);
  return SUCCESS;
}

Status HixlCSServer::Finalize() {
  if (listener_running_) {
    listener_running_ = false;
    if (listener_.joinable()) {
      listener_.join();
    }
  }
  msg_handler_.Finalize();
  auto ret = endpoint_store_.Finalize();
  HIXL_CHK_STATUS(ret, "Failed to finalize endpoint store.");
  if (trans_flag_ != nullptr) {
    auto rt_ret = rtFree(trans_flag_);
    if (rt_ret != RT_ERROR_NONE) {
      HIXL_LOGE(FAILED, "Failed to free trans finished flag, ret:%d", rt_ret);
    }
    trans_flag_ = nullptr;
  }

  if (listen_fd_ != -1) {
    (void)close(listen_fd_);
    listen_fd_ = -1;
  }

  if (epoll_fd_ != -1) {
    (void)close(epoll_fd_);
    epoll_fd_ = -1;
  }
  HIXL_EVENT("[HixlServer] finalize end, ret:%u", ret);
  return ret;
}

Status HixlCSServer::RegisterMem(const char *mem_tag, const HcclMem *mem, MemHandle *mem_handle) {
  HIXL_EVENT("[HixlServer] register mem start, addr:%p, size:%lu, type:%d",
             mem->addr, mem->size, static_cast<int32_t>(mem->type));
  auto all_handles = endpoint_store_.GetAllEndpointHandles();
  HIXL_CHK_BOOL_RET_STATUS(all_handles.size() > 0, PARAM_INVALID, "no endpoint is available");
  std::vector<EndpointMemInfo> ep_mem_infos;
  for (auto handle : all_handles) {
    auto endpoint = endpoint_store_.GetEndpoint(handle);
    HIXL_CHECK_NOTNULL(endpoint);
    MemHandle ep_mem_handle = nullptr;
    HIXL_CHK_STATUS_RET(endpoint->RegisterMem(mem_tag, *mem, ep_mem_handle), "Failed to register mem.");
    EndpointMemInfo ep_mem_info{};
    ep_mem_info.endpoint_handle = handle;
    ep_mem_info.mem_handle = ep_mem_handle;
    ep_mem_infos.emplace_back(ep_mem_info);
  }
  *mem_handle = ep_mem_infos[0].mem_handle;
  HIXL_EVENT("[HixlServer] register mem success, addr:%p, size:%lu, type:%d, handle:%p",
             mem->addr, mem->size, static_cast<int32_t>(mem->type), *mem_handle);
  std::lock_guard<std::mutex> lock(reg_mutex_);
  reg_mems_[ep_mem_infos[0].mem_handle] = std::move(ep_mem_infos);
  return SUCCESS;
}

Status HixlCSServer::DeregisterMem(MemHandle mem_handle) {
  HIXL_EVENT("[HixlServer] deregister mem start, handle:%p", mem_handle);
  std::lock_guard<std::mutex> lock(reg_mutex_);
  auto it = reg_mems_.find(mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(it != reg_mems_.cend(), PARAM_INVALID, "mem_handle:%p is not registed", mem_handle);
  for (const auto &ep_mem_info : it->second) {
    auto endpoint = endpoint_store_.GetEndpoint(ep_mem_info.endpoint_handle);
    HIXL_CHECK_NOTNULL(endpoint);
    HIXL_CHK_STATUS_RET(endpoint->DeregisterMem(ep_mem_info.mem_handle), "Failed to deregister mem, handle:%p.",
                        ep_mem_info.mem_handle);
  }
  reg_mems_.erase(it);
  HIXL_EVENT("[HixlServer] deregister mem success, handle:%p", mem_handle);
  return SUCCESS;
}

Status HixlCSServer::SendCreateChannelResp(int32_t fd,
                                           const CreateChannelResp &resp) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(
      sizeof(CtrlMsgType) + sizeof(CreateChannelResp));
  CtrlMsgType msg_type = CtrlMsgType::kCreateChannelResp;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &resp, static_cast<uint64_t>(sizeof(resp))));
  return SUCCESS;
}

Status HixlCSServer::CreateChannel(int32_t fd, const char *msg, uint64_t msg_len) {
  HIXL_DISMISSABLE_GUARD(failed, ([fd, this]() {
    CreateChannelResp resp{};
    resp.result = FAILED;
    HIXL_CHK_STATUS(SendCreateChannelResp(fd, resp));
  }));
  HIXL_CHECK_NOTNULL(msg);
  HIXL_CHK_BOOL_RET_STATUS(msg_len == sizeof(CreateChannelReq), PARAM_INVALID,
                           "invalid msg len:%lu of create channel, must = %zu", msg_len, sizeof(CreateChannelReq));
  const auto &req = *reinterpret_cast<const CreateChannelReq *>(msg);
  EndPointHandle handle = nullptr;
  auto ep = endpoint_store_.MatchEndpoint(req.dst, handle);
  HIXL_CHECK_NOTNULL(ep);
  CreateChannelResp resp{};
  resp.dst_ep_handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
  ChannelHandle channel_handle = 0UL;
  HIXL_CHK_STATUS_RET(ep->CreateChannel(req.src, channel_handle), "Failed to create channel");
  std::lock_guard<std::mutex> lock(chn_mutex_);
  EndpointChannelInfo info{};
  info.endpoint_handle = handle;
  info.channel_handle = channel_handle;
  channels_[fd] = std::move(info);
  HIXL_DISMISS_GUARD(failed);
  resp.result = SUCCESS;
  HIXL_CHK_STATUS_RET(SendCreateChannelResp(fd, resp), "Failed to send create channel resp");
  return SUCCESS;
}

Status HixlCSServer::DestroyChannel(int32_t fd, const char *msg, uint64_t msg_len) {
  (void)msg;
  (void)msg_len;
  std::lock_guard<std::mutex> lock(chn_mutex_);
  auto it = channels_.find(fd);
  if (it != channels_.end()) {
    auto handle = it->second.endpoint_handle;
    auto ep = endpoint_store_.GetEndpoint(handle);
    HIXL_CHECK_NOTNULL(ep);
    HIXL_CHK_STATUS_RET(ep->DestroyChannel(it->second.channel_handle), "Failed to destroy channel");
    channels_.erase(it);
  }
  return SUCCESS;
}

static inline void to_json(nlohmann::json &j, const HixlMemDesc &m) {
  j = nlohmann::json{};
  j["mem"] = m.mem;
  j["tag"] = m.tag;
  j["export_desc"] = nlohmann::json::array();
  if (m.export_desc != nullptr && m.export_len > 0) {
    const uint8_t* data_ptr = static_cast<const uint8_t*>(m.export_desc);
    for (size_t i = 0; i < m.export_len; ++i) {
      j["export_desc"].push_back(static_cast<int>(data_ptr[i]));
    }
  }
}

static inline void to_json(nlohmann::json &j, const GetRemoteMemResp &r) {
  j = nlohmann::json{};
  j["result"] = r.result;
  j["mem_descs"] = r.mem_descs;
}

template <typename T>
Status HixlCSServer::Serialize(const T &msg, std::string &msg_str) {
  try {
    nlohmann::json j = msg;
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to dump msg to str, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status HixlCSServer::SendRemoteMemResp(int32_t fd,
                                       const GetRemoteMemResp &resp) {
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  std::string msg_str;
  HIXL_CHK_STATUS_RET(Serialize(resp, msg_str), "Failed to serialize msg");
  HIXL_LOGI("remote mem serialize success, str:%s", msg_str.c_str());
  header.body_size = static_cast<uint64_t>(
      sizeof(CtrlMsgType) + msg_str.size());
  CtrlMsgType msg_type = CtrlMsgType::kGetRemoteMemResp;
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, &msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(fd, msg_str.c_str(), static_cast<uint64_t>(msg_str.size())));
  return SUCCESS;
}

Status HixlCSServer::GetRemoteMem(int32_t fd, const char *msg, uint64_t msg_len) {
  HIXL_DISMISSABLE_GUARD(failed, ([fd, this]() {
    GetRemoteMemResp resp{};
    resp.result = FAILED;
    HIXL_CHK_STATUS(SendRemoteMemResp(fd, resp));
  }));
  HIXL_CHECK_NOTNULL(msg);
  HIXL_CHK_BOOL_RET_STATUS(msg_len == sizeof(GetRemoteMemReq), PARAM_INVALID, "invalid msg len:%lu of get remote mem",
                           msg_len);
  const auto req = reinterpret_cast<const GetRemoteMemReq *>(msg);
  EndPointHandle handle = reinterpret_cast<EndPointHandle>(static_cast<intptr_t>(req->dst_ep_handle));
  HIXL_CHECK_NOTNULL(handle);
  auto ep = endpoint_store_.GetEndpoint(handle);
  HIXL_CHECK_NOTNULL(ep);
  GetRemoteMemResp resp{};
  HIXL_CHK_STATUS_RET(ep->ExportMem(resp.mem_descs), "Failed to export mem");
  resp.result = SUCCESS;
  HIXL_DISMISS_GUARD(failed);
  HIXL_CHK_STATUS_RET(SendRemoteMemResp(fd, resp), "Failed to send remote mem resp.");
  return SUCCESS;
}

Status HixlCSServer::Listen(uint32_t backlog) {
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Listen(ip_, port_, backlog, listen_fd_), "Failed to server listen");
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::AddFdToEpoll(epoll_fd_, listen_fd_), "Failed to add listen fd to epoll");
  HIXL_EVENT("[HixlServer] start to listen on %s:%u", ip_.c_str(), port_);
  listener_running_ = true;
  listener_ = std::thread([this]() {
    while (listener_running_) {
      (void)DoWait();
    }
    return;
  });
  return SUCCESS;
}

Status HixlCSServer::RegProc(CtrlMsgType msg_type, MsgProcessor proc) {
  HIXL_CHK_STATUS_RET(msg_handler_.RegisterMsgProcessor(msg_type, proc),
                      "Failed to reg msg processor, msg type:%d", static_cast<int32_t>(msg_type));
  return SUCCESS;
}

void HixlCSServer::ProClientMsg(int32_t fd, std::shared_ptr<MsgReceiver> receiver) {
  std::vector<CtrlMsgPtr> msgs;
  (void)receiver->IRecv(msgs);
  for (auto msg : msgs) {
    if (msg->msg_type == CtrlMsgType::kDestroyChannelReq) {
      epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
      (void)close(fd);
      clients_.erase(fd);
    }
    msg_handler_.SubmitMsg(fd, msg);
  }
}

Status HixlCSServer::DoWait() {
  struct epoll_event event_infos[kMaxEventsNum];
  int32_t event_num = epoll_wait(epoll_fd_, event_infos, kMaxEventsNum, kEpollWaitTimeInMillis);
  for (int32_t i = 0; i < event_num; ++i) {
    // new connection
    int32_t fd = event_infos[i].data.fd;
    uint32_t revents = event_infos[i].events;
    if (fd == listen_fd_) {
      int32_t connect_fd = -1;
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Accept(listen_fd_, connect_fd), "Failed to accept fd");
      HIXL_CHK_STATUS_RET(CtrlMsgPlugin::AddFdToEpoll(epoll_fd_, connect_fd), "Failed to add connect fd to epoll");
      HIXL_EVENT("[HixlServer] accept socket success, client fd:%d", connect_fd);
      auto receiver = MakeShared<MsgReceiver>(connect_fd);
      HIXL_CHECK_NOTNULL(receiver);
      std::lock_guard<std::mutex> lock(client_mutex_);
      clients_[connect_fd] = receiver;
    } else {
      // client msg
      std::lock_guard<std::mutex> lock(client_mutex_);
      auto it = clients_.find(fd);
      HIXL_EVENT("[HixlServer] recv socket msg, client fd:%d", fd);
      if ((it != clients_.end()) && ((revents & EPOLLIN) != 0U)) {
        ProClientMsg(fd, it->second);
      }
    }
  }
  return SUCCESS;
}
}  // namespace hixl
