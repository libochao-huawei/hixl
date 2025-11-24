/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel_manager.h"
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <cstring>
#include <utility>
#include "common/mem_utils.h"
#include "common/llm_scope_guard.h"
#include "common/def_types.h"
#include "base/err_msg.h"
#include "control_msg_handler.h"

namespace adxl {
namespace {
constexpr int64_t kWaitTimeInMillis = 10000;
constexpr int64_t kSendMsgTimeout = 1000000;
constexpr int32_t kMaxEvents = 1024;
const size_t kRecvChunkSize = 4096;
constexpr int32_t kEpollWaitTimeInMillis = 1000;
}

int64_t ChannelManager::wait_time_in_millis_ = kWaitTimeInMillis;

Status ChannelManager::Initialize(BufferTransferService *buffer_transfer_service) {
  ADXL_CHK_ACL_RET(rtCtxGetCurrent(&rt_context_));
  buffer_transfer_service_ = buffer_transfer_service;
  epoll_fd_ = epoll_create1(0);
  if (epoll_fd_ == -1) {
    LLMLOGE(FAILED, "Failed to create epoll fd.");
    return FAILED;
  }
  // send heartbeat periodically
  heartbeat_sender_ = std::thread([this]() {
    std::unique_lock<std::mutex> lock(cv_mutex_);
    while (!stop_signal_.load()) {
      SendHeartbeats();
      cv_.wait_for(lock, std::chrono::milliseconds(wait_time_in_millis_), [this] { return stop_signal_.load(); });
    }
  });
  // receive msg thread
  msg_receiver_ = std::thread([this]() {
    rtCtxSetCurrent(rt_context_);
    while (!stop_signal_.load()) {
      HandleEpoolEvents();
      CheckHeartbeatTimeouts();
    }
  });
  return SUCCESS;
}

Status ChannelManager::AddSocketToEpoll(int32_t fd, ChannelPtr channel) {
  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
    LLMLOGE(FAILED, "Failed to add fd %d to epoll: %s", fd, strerror(errno));
    return FAILED;
  }

  std::lock_guard<std::mutex> lock(fd_mutex_);
  fd_to_channel_map_[fd] = std::move(channel);
  LLMLOGI("Successfully added fd %d to epoll", fd);
  return SUCCESS;
}

Status ChannelManager::HandleEpoolEvents() {
  struct epoll_event events[kMaxEvents];
  int nfds = epoll_wait(epoll_fd_, events, kMaxEvents, kEpollWaitTimeInMillis);
  if (nfds == -1) {
    LLMLOGE(FAILED, "epoll_wait error: %s", strerror(errno));
    return FAILED;
  }
  if (nfds == 0) {
    return SUCCESS;
  }
  for (int i = 0; i < nfds; ++i) {
    ADXL_CHK_STATUS_RET(HandleSocketEvent(events[i].data.fd), "Failed to handle socket event.");
  }
  return SUCCESS;
}

Status ChannelManager::HandleSocketEvent(int32_t fd) {
  std::lock_guard<std::mutex> lock(fd_mutex_);
  auto it = fd_to_channel_map_.find(fd);
  auto ret = SUCCESS;
  if (it != fd_to_channel_map_.end()) {
    ret = HandleReadEvent(it->second);
  }
  return ret;
}

Status ChannelManager::HandleReadEvent(const ChannelPtr &channel) {
  int fd = channel->GetFd();
  if (channel->recv_buffer_.size() < channel->bytes_received_ + kRecvChunkSize) {
    channel->recv_buffer_.resize(channel->bytes_received_ + kRecvChunkSize);
  }
  ssize_t n = recv(fd, channel->recv_buffer_.data() + channel->bytes_received_,
                   channel->recv_buffer_.size() - channel->bytes_received_, 0);
  if (n == 0) {
    LLMLOGI("Connection closed by peer, fd: %d, channel:%s.", fd, channel->GetChannelId().c_str());
    return SUCCESS;
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EINTR) {
      return SUCCESS;
    }
    LLMLOGE(FAILED, "recv error on channel:%s, errno:%s", channel->GetChannelId().c_str(), strerror(errno));
    return FAILED;
  }
  channel->bytes_received_ += n;
  return ProcessReceivedData(channel);
}

Status ChannelManager::ProcessReceivedData(const ChannelPtr &channel) {
  while (true) {
    if (channel->recv_state_ == RecvState::WAITING_FOR_HEADER) {
      if (channel->bytes_received_ < sizeof(ProtocolHeader)) {
        break;
      }
      ProtocolHeader *header = nullptr;
      header = llm::PtrToPtr<char, ProtocolHeader>(channel->recv_buffer_.data());
      if (header->magic != kMagicNumber) {
        LLMLOGE(FAILED, "Invalid magic number received on channel:%s.", channel->GetChannelId().c_str());
        return RemoveFd(channel->GetFd());
      }
      channel->expected_body_size_ = header->body_size;
      channel->recv_state_ = RecvState::WAITING_FOR_BODY;

      if (channel->bytes_received_ > sizeof(ProtocolHeader)) {
        size_t remaining = channel->bytes_received_ - sizeof(ProtocolHeader);
        memmove_s(channel->recv_buffer_.data(), remaining, channel->recv_buffer_.data() + sizeof(ProtocolHeader),
                  remaining);
        channel->bytes_received_ = remaining;
      } else {
        channel->bytes_received_ = 0;
      }
    }
    if (channel->recv_state_ == RecvState::WAITING_FOR_BODY) {
      if (channel->bytes_received_ < channel->expected_body_size_) {
        break;
      }
      ADXL_CHK_STATUS_RET(HandleControlMessage(channel),
                          "Failed to handle control message");

      if (channel->bytes_received_ > channel->expected_body_size_) {
        size_t remaining = channel->bytes_received_ - channel->expected_body_size_;
        memmove_s(channel->recv_buffer_.data(), remaining, channel->recv_buffer_.data() + channel->expected_body_size_,
                  remaining);
        channel->bytes_received_ = remaining;
        channel->recv_state_ = RecvState::WAITING_FOR_HEADER;
      } else {
        channel->bytes_received_ = 0;
        channel->recv_state_ = RecvState::WAITING_FOR_HEADER;
        break;
      }
    }
  }
  return SUCCESS;
}

Status ChannelManager::HandleControlMessage(const ChannelPtr &channel) const {
  ADXL_CHK_BOOL_RET_STATUS(channel->expected_body_size_ > sizeof(ControlMsgType), FAILED,
                           "Received msg invalid, channel:%s.", channel->GetChannelId().c_str());
  auto data = channel->recv_buffer_.data();
  ControlMsgType *msg_type = llm::PtrToPtr<char, ControlMsgType>(data);
  std::string msg_str(data + sizeof(ControlMsgType), channel->expected_body_size_ - sizeof(ControlMsgType));
  
  switch (*msg_type) {
    case ControlMsgType::kHeartBeat:
      return HandleHeartBeatMessage(channel);
    case ControlMsgType::kBufferReq:
      return HandleBufferReqMessage(channel, msg_str);
    case ControlMsgType::kBufferResp:
      return HandleBufferRespMessage(channel, msg_str);
    case ControlMsgType::kRequestDisconnect:
      return HandleRequestDisconnectMessage(channel, msg_str);
    case ControlMsgType::kRequestDisconnectResp:
      return HandleRequestDisconnectRespMessage(channel, msg_str);
    default:
      LLMLOGW("Unsupported msg type: %d", *msg_type);
      return SUCCESS;
  }
}

Status ChannelManager::HandleHeartBeatMessage(const ChannelPtr &channel) const {
  channel->UpdateHeartbeatTime();
  LLMLOGI("Heartbeat received from channel %s", channel->GetChannelId().c_str());
  return SUCCESS;
}

Status ChannelManager::HandleBufferReqMessage(const ChannelPtr &channel, const std::string &msg_str) const {
  BufferReq buffer_req{};
  ADXL_CHK_STATUS_RET(ControlMsgHandler::Deserialize(msg_str.c_str(), buffer_req), "Failed to deserialize msg");
  LLMLOGI("Recv buffer req for channel:%s", channel->GetChannelId().c_str());
  if (buffer_transfer_service_ != nullptr) {
    buffer_transfer_service_->PushBufferReq(channel, buffer_req);
  }
  return SUCCESS;
}

Status ChannelManager::HandleBufferRespMessage(const ChannelPtr &channel, const std::string &msg_str) const {
  BufferResp buffer_resp{};
  ADXL_CHK_STATUS_RET(ControlMsgHandler::Deserialize(msg_str.c_str(), buffer_resp), "Failed to deserialize msg");
  LLMLOGI("Recv buffer resp for channel:%s", channel->GetChannelId().c_str());
  if (buffer_transfer_service_ != nullptr) {
    channel->IncrementTransferCount();
    channel->SetHasTransferred(true);
    buffer_transfer_service_->PushBufferResp(channel, buffer_resp);
  }
  LLMLOGI("Recv buffer resp for channel:%s", channel->GetChannelId().c_str());
  return SUCCESS;
}

Status ChannelManager::HandleRequestDisconnectMessage(const ChannelPtr &channel, const std::string &msg_str) const {
  // Client端处理Server的断链请求
  RequestDisconnectMsg req_msg{};
  ADXL_CHK_STATUS_RET(ControlMsgHandler::Deserialize(msg_str.c_str(), req_msg), "Failed to deserialize RequestDisconnectMsg");
  LLMLOGI("Recv request disconnect for channel:%s, target:%s, req_id=%lu", 
          channel->GetChannelId().c_str(), req_msg.channel_id.c_str(), req_msg.req_id);
  
  // 验证channel_id是否匹配
  if (channel->GetChannelId() != req_msg.channel_id) {
    LLMLOGW("Channel id mismatch: local=%s, request=%s", channel->GetChannelId().c_str(), req_msg.channel_id.c_str());
    // 发送错误响应
    RequestDisconnectResp resp;
    resp.channel_id = req_msg.channel_id;
    resp.req_id = req_msg.req_id;
    resp.can_disconnect = false;
    resp.disconnected = false;
    resp.error_code = static_cast<uint32_t>(PARAM_INVALID);
    resp.error_message = "Channel id mismatch";
    (void)channel->SendControlMsg([&resp](int32_t fd) {
      return ControlMsgHandler::SendMsg(fd, ControlMsgType::kRequestDisconnectResp, resp, kSendMsgTimeout);
    });
    return PARAM_INVALID;
  }
  
  // 检查是否可以断链
  bool can_disconnect = (channel->GetTransferCount() == 0 && !channel->IsDisconnecting());
  RequestDisconnectResp resp;
  resp.channel_id = req_msg.channel_id;
  resp.req_id = req_msg.req_id;
  resp.can_disconnect = can_disconnect;
  resp.disconnected = false;
  resp.error_code = 0U;
  resp.error_message = "";
  
  // 如果可以断链，执行断链
  if (can_disconnect && disconnect_callback_) {
    int32_t timeout_ms = static_cast<int32_t>req_msg.timeout;
    resp.disconnected = true;
    Status send_ret = channel->SendControlMsg([&resp](int32_t fd) {
      return ControlMsgHandler::SendMsg(fd, ControlMsgType::kRequestDisconnectResp, resp, kSendMsgTimeout);
    });
    if(send_ret == SUCCESS) {
      LLMLOGI("Successfully send disconnect response.");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kSendMsgTimeout));
    Status ret = disconnect_callback_(req_msg.channel_id, timeout_ms);
    if (ret == SUCCESS) {
      LLMLOGI("Successfully disconnected channel %s by request", req_msg.channel_id.c_str());
    } else {
      resp.error_code = static_cast<uint32_t>(ret);
      resp.error_message = "Disconnect failed";
      LLMLOGW("Failed to disconnect channel %s by request, ret=%d", req_msg.channel_id.c_str(), ret);
    }
  } else if (!can_disconnect) {
    resp.error_code = static_cast<uint32_t>(FAILED);
    resp.error_message = "Channel is busy";
    LLMLOGW("Channel %s is busy, cannot disconnect. transfer_count=%d, disconnecting=%d", 
            req_msg.channel_id.c_str(), channel->GetTransferCount(), channel->IsDisconnecting());
  } else {
    resp.error_code = static_cast<uint32_t>(FAILED);
    resp.error_message = "Disconnect callback not set";
    LLMLOGW("Disconnect callback not set, cannot disconnect channel %s", req_msg.channel_id.c_str());
  }
  
  Status send_ret = channel->SendControlMsg([&resp](int32_t fd) {
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kRequestDisconnectResp, resp, kSendMsgTimeout);
  });
  if (send_ret != SUCCESS) {
    LLMLOGW("Failed to send disconnect response for channel %s", req_msg.channel_id.c_str());
  }
  return (resp.disconnected ? SUCCESS : FAILED);
}

Status ChannelManager::HandleRequestDisconnectRespMessage(const ChannelPtr &channel, const std::string &msg_str) const {
  RequestDisconnectResp resp{};
  ADXL_CHK_STATUS_RET(ControlMsgHandler::Deserialize(msg_str.c_str(), resp), "Failed to deserialize RequestDisconnectResp");
  LLMLOGI("Recv disconnect response for channel:%s, req_id=%lu, disconnected=%d", 
          channel->GetChannelId().c_str(), resp.req_id, resp.disconnected);
  if (disconnect_response_callback_) {
    disconnect_response_callback_(resp);
  }
  return SUCCESS;
}

Status ChannelManager::Finalize() {
  stop_signal_.store(true);
  cv_.notify_all();
  if (heartbeat_sender_.joinable()) {
    heartbeat_sender_.join();
  }
  if (msg_receiver_.joinable()) {
    msg_receiver_.join();
  }

  for (const auto &channel : GetAllServerChannel()) {
    (void)DestroyChannel(ChannelType::kServer, channel->GetChannelId());
  }
  for (const auto &channel : GetAllClientChannel()) {
    (void)DestroyChannel(ChannelType::kClient, channel->GetChannelId());
  }
  channels_.clear();
  return SUCCESS;
}

void ChannelManager::SetHeartbeatWaitTime(int32_t time_in_millis) {
  wait_time_in_millis_ = time_in_millis;
}

std::vector<ChannelPtr> ChannelManager::GetAllClientChannel() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelPtr> channels;
  for (const auto &it : channels_) {
    if (it.first.first == ChannelType::kClient) {
      channels.push_back(it.second);
    }
  }
  return channels;
}

std::vector<ChannelPtr> ChannelManager::GetAllServerChannel() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelPtr> channels;
  for (const auto &it : channels_) {
    if (it.first.first == ChannelType::kServer) {
      channels.push_back(it.second);
    }
  }
  return channels;
}

void ChannelManager::SendHeartbeats() {
  auto channels = GetAllClientChannel();
  for (const auto &channel : channels) {
    HeartbeatMsg msg{};
    msg.msg = 'H';
    LLMLOGI("Start to send heartbeat msg to:%s.", channel->GetChannelId().c_str());
    (void)channel->SendHeartBeat([&msg](int32_t fd) {
      return ControlMsgHandler::SendMsg(fd, ControlMsgType::kHeartBeat, msg, kSendMsgTimeout);
    });
  }
}

Status ChannelManager::CreateChannel(const ChannelInfo &channel_info, ChannelPtr &channel_ptr) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = channels_.find(std::make_pair(channel_info.channel_type, channel_info.channel_id));
    ADXL_CHK_BOOL_RET_STATUS(it == channels_.cend(), ALREADY_CONNECTED,
                             "Channel already exists, channel_type = %d, channel id:%s",
                             static_cast<int32_t>(channel_info.channel_type), channel_info.channel_id.c_str());
  }
  ChannelPtr channel = llm::MakeShared<Channel>(channel_info);
  ADXL_CHECK_NOTNULL(channel);
  ADXL_CHK_STATUS_RET(channel->Initialize(), "Failed to init channel");
  LLM_DISMISSABLE_GUARD(failed_guard, ([channel]() { (void) channel->Finalize(); }));
  std::lock_guard<std::mutex> lock(mutex_);
  auto key = std::make_pair(channel_info.channel_type, channel_info.channel_id);
  const auto &it = channels_.find(key);
  ADXL_CHK_BOOL_RET_STATUS(it == channels_.cend(), ALREADY_CONNECTED,
                           "Channel already exists, channel_type = %d, channel id:%s",
                           static_cast<int32_t>(channel_info.channel_type), channel_info.channel_id.c_str());
  (void)channels_.emplace(std::make_pair(channel_info.channel_type, channel_info.channel_id), channel);
  channel_ptr = channel;
  LLMLOGI("Create channel success, channel_type = %d, channel id = %s",
         static_cast<int32_t>(channel_info.channel_type), channel_info.channel_id.c_str());
  LLM_DISMISS_GUARD(failed_guard);
  return SUCCESS;
}

void ChannelManager::CheckHeartbeatTimeouts() {
  std::vector<ChannelPtr> timeout_channels;
  auto channels = GetAllServerChannel();
  for (const auto &it : channels) {
    if (it->IsHeartbeatTimeout()) {
      timeout_channels.push_back(it);
    }
  }
  for (const auto &timeout_channel : timeout_channels) {
    LLMEVENT("Destroy timeout channel:%s.", timeout_channel->GetChannelId().c_str());
    (void) DestroyChannel(ChannelType::kServer, timeout_channel->GetChannelId());
  }
}

ChannelPtr ChannelManager::GetChannel(ChannelType channel_type, const std::string &channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = channels_.find(std::make_pair(channel_type, channel_id));
  if (it != channels_.cend()) {
    return it->second;
  }
  return nullptr;
}

Status ChannelManager::DestroyChannel(ChannelType channel_type, const std::string &channel_id) {
  auto ret = SUCCESS;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = channels_.find(std::make_pair(channel_type, channel_id));
  if (it != channels_.cend()) {
    auto channel = it->second;
    (void)RemoveFd(channel->GetFd());
    auto channel_ret = channel->Finalize();
    ret = channel_ret != SUCCESS ? channel_ret : ret;
    channels_.erase(it);
    LLMLOGI("Destroy channel end, channel_type = %d, channel_id = %s",
           static_cast<int32_t>(channel_type), channel_id.c_str());
  }
  return ret;
}

Status ChannelManager::RemoveFd(int32_t fd) {
  auto ret = SUCCESS;
  if (fd != -1) {
    std::lock_guard<std::mutex> fd_lock(fd_mutex_);
    auto fd_it = fd_to_channel_map_.find(fd);
    if (fd_it != fd_to_channel_map_.end()) {
      fd_to_channel_map_.erase(fd_it);
      auto epoll_ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
      if (epoll_ret == -1) {
        LLMLOGW("Failed to remove fd %d from epoll: %s", fd, strerror(errno));
      }
    }
  }
  return ret;
}

}  // namespace adxl
