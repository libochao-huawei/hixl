/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel_evictor.h"
#include <algorithm>
#include "nlohmann/json.hpp"
#include "common/msg_handler_plugin.h"
#include <common/llm_utils.h>
#include "common/llm_scope_guard.h"
#include <adxl/adxl_types.h>
#include "adxl_utils.h"

namespace adxl {
namespace {
  constexpr int32_t kWaitRespTime = 20;
} // namespace

ChannelEvictor::ChannelEvictor(ChannelManager* channel_manager)
  : channel_manager_(channel_manager) {}

Status ChannelEvictor::Initialize(const std::map<AscendString, AscendString>& options) {
  ADXL_CHECK_NOTNULL(channel_manager_);
  ADXL_CHK_STATUS_RET(ParseMaxChannel(options), "Failed to parse max channel configuration");
  ADXL_CHK_STATUS_RET(ParseHighWaterline(options), "Failed to parse high waterline configuration");
  ADXL_CHK_STATUS_RET(ParseLowWaterline(options), "Failed to parse low waterline configuration");
  ADXL_CHK_BOOL_RET_STATUS(low_waterline_ratio_ < high_waterline_ratio_, PARAM_INVALID,
               "low_waterline (%.2f) must be less than high_waterline (%.2f)",
               low_waterline_ratio_, high_waterline_ratio_);
  
  high_waterline_ = static_cast<int32_t>(max_channel_ * high_waterline_ratio_);
  low_waterline_ = static_cast<int32_t>(max_channel_ * low_waterline_ratio_);
  
  high_waterline_ = std::max(static_cast<int32_t>(1), high_waterline_);
  low_waterline_ = std::max(static_cast<int32_t>(1), low_waterline_);
  
  ADXL_CHK_BOOL_RET_STATUS(low_waterline_ <= high_waterline_, PARAM_INVALID,
               "low_waterline (%d) must be less than high_waterline (%d)",
               low_waterline_, high_waterline_);
  ADXL_CHK_BOOL_RET_STATUS(high_waterline_ <= max_channel_, PARAM_INVALID,
               "high_waterline (%d) must be less than or equal to max_channel (%d)",
               high_waterline_, max_channel_);
  
  LLMLOGI("Waterline config: max_channel=%d, high_waterline=%.2f (%d), low_waterline=%.2f (%d)",
      max_channel_, high_waterline_ratio_, high_waterline_, 
      low_waterline_ratio_, low_waterline_);
  ADXL_CHK_STATUS_RET(StartEvictionThread(), "Failed to start eviction thread");
  ADXL_CHK_STATUS_RET(SetupChannelManagerCallbacks(), "Failed to setup channel manager callbacks");
  return SUCCESS;
}

Status ChannelEvictor::ParseMaxChannel(const std::map<AscendString, AscendString>& options) {
  auto max_it = options.find(OPTION_MAX_CHANNEL);
  if (max_it != options.end()) {
    std::string max_str = max_it->second.GetString();
    if (llm::LLMUtils::ToNumber(max_str, max_channel_)) {
      LLMLOGE(PARAM_INVALID, "Invalid max_channel: %s", max_str.c_str());
      return PARAM_INVALID;
    }
    if (max_channel_ <= 0) {
      LLMLOGE(PARAM_INVALID, "Invalid max_channel: %d (must be > 0)", max_channel_);
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}

Status ChannelEvictor::ParseHighWaterline(const std::map<AscendString, AscendString>& options) {
  auto high_it = options.find(OPTION_HIGH_WATERLINE);
  if (high_it != options.end()) {
    std::string high_str = high_it->second.GetString();
    double high_value = 0.0;
    if (llm::LLMUtils::ToNumber(high_str, high_value)) {
      LLMLOGE(PARAM_INVALID, "Invalid high_waterline: %s", high_str.c_str());
      return PARAM_INVALID;
    }
    if (high_value <= 0.0 || high_value > 1.0) {
      LLMLOGE(PARAM_INVALID, "Invalid high_waterline: %.2f (must be 0~1)", high_value);
      return PARAM_INVALID;
    }
    high_waterline_ratio_ = high_value;
  } else {
    high_waterline_ratio_ = kDefaultHighWaterline;
  }
  return SUCCESS;
}

Status ChannelEvictor::ParseLowWaterline(const std::map<AscendString, AscendString>& options) {
  auto low_it = options.find(OPTION_LOW_WATERLINE);
  if (low_it != options.end()) {
    std::string low_str = low_it->second.GetString();
    double low_value = 0.0;
    if (llm::LLMUtils::ToNumber(low_str, low_value)) {
      LLMLOGE(PARAM_INVALID, "Invalid low_waterline: %s", low_str.c_str());
      return PARAM_INVALID;
    }
    if (low_value <= 0.0 || low_value > 1.0) {
      LLMLOGE(PARAM_INVALID, "Invalid low_waterline: %.2f (must be 0~1)", low_value);
      return PARAM_INVALID;
    }
    low_waterline_ratio_ = low_value;
  } else {
    low_waterline_ratio_ = kDefaultLowWaterline;
  }
  return SUCCESS;
}

Status ChannelEvictor::StartEvictionThread() {
  if (high_waterline_ > 0 && low_waterline_ > 0) {
    stop_eviction_ = false;
    eviction_thread_ = std::thread([this]() { 
      EvictionLoop(); 
    });
    LLMLOGI("Eviction thread started with waterline: max=%d, high=%d, low=%d", 
        max_channel_, high_waterline_, low_waterline_);
  }
  return SUCCESS;
}

Status ChannelEvictor::SetupChannelManagerCallbacks() {
  channel_manager_->SetDisconnectCallback([this](const std::string& channel_id, int32_t timeout_ms) {
    EvictItem item;
    item.channel_id = channel_id;
    auto client_channel = channel_manager_->GetChannel(ChannelType::kClient, channel_id);
    if (client_channel != nullptr) {
      item.channel_type = ChannelType::kClient;
    } else {
      LLMLOGI("Channel %s not found for disconnect", channel_id.c_str());
      return NOT_CONNECTED;
    }
    item.timeout_ms = timeout_ms;
    {
      std::lock_guard<std::mutex> lock(evict_mutex_);
        evict_queue_.push(item);
    }
    evict_cv_.notify_one();
    return SUCCESS;
  });

  channel_manager_->SetDisconnectResponseCallback([this](const RequestDisconnectResp& resp) {
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    auto it = pending_disconnect_requests_.find(resp.req_id);
    if (it != pending_disconnect_requests_.end()) {
      it->second->resp = resp;
      it->second->received = true;
      it->second->cv.notify_one();
    }
  });
  return SUCCESS;
}

Status ChannelEvictor::SetListenInfo(const std::string listen_info) {
  listen_info_ = listen_info;
  return SUCCESS;
}

Status ChannelEvictor::Finalize() {
  if (eviction_thread_.joinable()) {
    {
      std::lock_guard<std::mutex> lock(evict_mutex_);
      stop_eviction_ = true;
    }
    evict_cv_.notify_all();
    eviction_thread_.join();
  }
  return SUCCESS;
}

int32_t ChannelEvictor::GetTotalChannelCount() const {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  return static_cast<int32_t>(client_channels.size() + server_channels.size());
}

bool ChannelEvictor::ShouldTriggerEviction() const {
  bool should_evict = GetTotalChannelCount() >= high_waterline_;
  if (should_evict) {
    LLMLOGI("Eviction triggered: current_channel_count=%d >= high_waterline=%d", 
            GetTotalChannelCount(), high_waterline_);
  }
  return should_evict;
}

Status ChannelEvictor::NotifyEviction() {
  if (!ShouldTriggerEviction()) {
    return SUCCESS;
  }

  int32_t current_count = GetTotalChannelCount();
  int32_t need_expire = current_count - low_waterline_;
  if (need_expire <= 0) {
    LLMLOGI("No need to evict channels: current_channel_count=%d - low_waterline=%d <= 0", 
            current_count, low_waterline_);
    return SUCCESS;
  }

  std::vector<EvictItem> candidates = SelectEvictionCandidates(need_expire);
  LLMLOGI("Select %zu eviction candidates from %d total channels", 
          candidates.size(), current_count);
  std::lock_guard<std::mutex> lock(evict_mutex_);
  for (const auto& item : candidates) {
    evict_queue_.push(item);
  }
  evict_cv_.notify_one();
  return SUCCESS;
}

std::vector<EvictItem> ChannelEvictor::SelectEvictionCandidates(int32_t need_expire) {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  
  LLMLOGI("SelectEvictionCandidates: need_expire=%d, client_channels=%zu, server_channels=%zu", 
          need_expire, client_channels.size(), server_channels.size());
  
  std::sort(client_channels.begin(), client_channels.end(), 
      [](const ChannelPtr& a, const ChannelPtr& b) {
        if (a->GetHasTransferred() != b->GetHasTransferred()) {
          return !a->GetHasTransferred(); 
        }
        return false;
      });
  std::vector<EvictItem> target_items;
  target_items.reserve(need_expire);
  auto client_it = client_channels.begin();
  auto server_it = server_channels.begin();
  auto client_end = client_channels.end();
  auto server_end = server_channels.end();
  int32_t diff = std::abs(static_cast<int32_t>(client_channels.size() - server_channels.size()));
  int32_t pick_extra = std::min(diff, need_expire);
  bool pick_client_first = (client_channels.size() > server_channels.size());

  for(int32_t i = 0; i < pick_extra && need_expire > 0; i++) {
    if (pick_client_first && client_it != client_end) {
      target_items.push_back(EvictItem{(*client_it)->GetChannelId(), ChannelType::kClient});
      (*client_it)->SetDisconnecting(true);
      ++client_it;
    } else if (server_it != server_end) {
      target_items.push_back(EvictItem{(*server_it)->GetChannelId(), ChannelType::kServer});
      (*server_it)->SetDisconnecting(true);
      ++server_it;
    }
    need_expire--;
  }
  bool pick_client = true;
  while(need_expire > 0 && (client_it != client_end || server_it != server_end)) {
    if (pick_client && client_it != client_end) {
      target_items.push_back(EvictItem{(*client_it)->GetChannelId(), ChannelType::kClient});
      (*client_it)->SetDisconnecting(true);
      ++client_it;
    } else if (!pick_client && server_it != server_end) {
      target_items.push_back(EvictItem{(*server_it)->GetChannelId(), ChannelType::kServer});
      (*server_it)->SetDisconnecting(true);
      ++server_it;
    }
    need_expire--;
    pick_client = !pick_client;
  }
  
  return target_items;
}

void ChannelEvictor::EvictionLoop() {
  while (true) {
    std::unique_lock<std::mutex> lock(evict_mutex_);
    evict_cv_.wait(lock, [this] { 
      return stop_eviction_ || !evict_queue_.empty(); 
    });
    if (stop_eviction_) {
      break;
    }
    
    while (!evict_queue_.empty()) {
      EvictItem item = evict_queue_.front();
      evict_queue_.pop();
      lock.unlock();
      ProcessEviction(item);
      lock.lock();
    }
    ResetAllTransferFlags();
  }
}

Status ChannelEvictor::ProcessEviction(const EvictItem& item) {
  auto channel = channel_manager_->GetChannel(item.channel_type, item.channel_id);
  if (channel == nullptr) {
    LLMLOGI("Skip eviction: channel %s (type:%d) not found", 
            item.channel_id.c_str(), static_cast<int>(item.channel_type));
    return SUCCESS;
  }
  
  if (channel->GetTransferCount() > 0) {
    LLMLOGI("Skip eviction: channel %s has unfinished transfers (count:%d)", 
            item.channel_id.c_str(), channel->GetTransferCount());
    return SUCCESS;
  }
  if (item.channel_type == ChannelType::kServer) {
    return ProcessServerEviction(item.channel_id, channel);
  } else {
    return ProcessClientEviction(item.channel_id, item.timeout_ms);
  }
}

Status ChannelEvictor::ProcessServerEviction(const std::string& channel_id, ChannelPtr channel) {
  int32_t fd = channel->GetFd();
  if (fd < 0) {
    LLMLOGW("Channel %s has invalid fd, cannot send request disconnect", channel_id.c_str());
    return SUCCESS;
  }
  uint64_t req_id = next_req_id_.fetch_add(1ULL, std::memory_order_acq_rel);
  auto pending_req = std::make_shared<PendingDisconnectRequest>();
  {
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    pending_disconnect_requests_[req_id] = pending_req;
  }
  RequestDisconnectMsg req_msg;
  req_msg.channel_id = listen_info_;
  req_msg.timeout = 1000ULL;
  req_msg.req_id = req_id;
  Status ret = channel->SendControlMsg([&req_msg](int32_t fd) {
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kRequestDisconnect, req_msg, req_msg.timeout);
  });
  if (ret != SUCCESS) {
    LLMLOGW("Failed to send request disconnect for channel: %s, ret=%d", channel_id.c_str(), ret);
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    pending_disconnect_requests_.erase(req_id);
    return ret;
  }
  LLMLOGI("Sent request disconnect to client for channel: %s, req_id=%lu", channel_id.c_str(), req_id);
  std::unique_lock<std::mutex> lock(pending_req_mutex_);
  bool received = pending_req->cv.wait_for(lock, std::chrono::milliseconds(kWaitRespTime), [&pending_req] {
    return pending_req->received;
  });
  if (!received) {
    pending_disconnect_requests_.erase(req_id);
    return SUCCESS;
  } else {
    RequestDisconnectResp resp = pending_req->resp;
    pending_disconnect_requests_.erase(req_id);
    lock.unlock();
    LLMLOGI("Client refused or failed to disconnect channel %s, error_code=%u, error_message=%s", 
      channel_id.c_str(), resp.error_code, resp.error_message.c_str());
    return SUCCESS;
  }
}

Status ChannelEvictor::ProcessClientEviction(const std::string& channel_id, int32_t timeout_ms) {
  Status ret = Disconnect(channel_id, timeout_ms);
  if (ret == SUCCESS) {
    LLMLOGI("Evicted client channel: %s", channel_id.c_str());
  } else {
    LLMLOGI("Failed to evict client channel: %s, ret=%d", channel_id.c_str(), ret);
  }
  return ret;
}

Status ChannelEvictor::ResetAllTransferFlags() {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  for (auto& channel : client_channels) {
    channel->SetHasTransferred(false);
  }
  for (auto& channel : server_channels) {
    channel->SetHasTransferred(false);
  }
  LLMLOGI("Reset all transfer flags, client=%zu, server=%zu", 
    client_channels.size(), server_channels.size());
  return SUCCESS;
}

Status ChannelEvictor::Disconnect(const std::string &remote_engine, int32_t timeout_in_millis) {
  std::string remote_ip;
  int32_t remote_port = -1;
  ADXL_CHK_STATUS_RET(ParseListenInfo(remote_engine, remote_ip, remote_port), "Failed to parse listen info");
  LLMEVENT("Start to disconnect, local engine:%s, remote engine:%s, timeout:%d ms.",
          listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);

  int32_t conn_fd = -1;
  LLM_MAKE_GUARD(close_fd, ([&conn_fd]() {
    if (conn_fd != -1) {
      llm::MsgHandlerPlugin::Disconnect(conn_fd);
    }
  }));

  auto channel = channel_manager_->GetChannel(ChannelType::kClient, remote_engine);
  ADXL_CHK_BOOL_RET_STATUS(channel != nullptr, NOT_CONNECTED,
                           "Failed to get channel, channel_id:%s", remote_engine.c_str());
  std::lock_guard<std::mutex> transfer_lock(channel->GetTransferMutex());
  channel->StopHeartbeat();
  // if connect failed, then release client and server auto release channel
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::Connect(remote_ip, static_cast<uint32_t>(remote_port),
                                                  conn_fd, timeout_in_millis, SUCCESS),
                   "Failed to connect remote addr %s:%d, timeout=%d ms.",
                   remote_ip.c_str(), remote_port, timeout_in_millis);
  Status send_status = FAILED;
  if (conn_fd > 0) {
    EvictChannelDisconnectInfo disconnect_info = {};
    disconnect_info.channel_id = listen_info_;
    send_status = SendMsg(conn_fd, EvictChannelMsgType::kDisconnect, disconnect_info);
  }

  EvictChannelDisconnectInfo local_disconnect_info = {};
  local_disconnect_info.channel_id = remote_engine;
  auto ret = DisconnectInfoProcess(ChannelType::kClient, local_disconnect_info);
  if (send_status == SUCCESS) {
    EvictChannelStatus status{};
    ADXL_CHK_STATUS_RET(RecvMsg(conn_fd, EvictChannelMsgType::kStatus, status),
                        "Failed to recv status msg");
    ADXL_CHK_STATUS_RET(status.error_code, "Failed to check peer process ret status, error code[%u], err msg[%s]",
                        status.error_code, status.error_message.c_str());
  }
  ADXL_CHK_STATUS_RET(ret, "Failed to disconnect, local engine:%s, remote engine:%s, timeout:%d ms.",
                      listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  LLMEVENT("Success to disconnect, local engine:%s, remote engine:%s, timeout:%d ms.",
          listen_info_.c_str(), remote_engine.c_str(), timeout_in_millis);
  return SUCCESS;
}

Status ChannelEvictor::DisconnectInfoProcess(ChannelType channel_type,
                                                const EvictChannelDisconnectInfo &peer_disconnect_info) {
  LLMLOGI("Destroy channel in disconnect process.");
  return channel_manager_->DestroyChannel(channel_type, peer_disconnect_info.channel_id);
}

Status ChannelEvictor::ParseListenInfo(const std::string &listen_info,
                                          std::string &listen_ip, int32_t &listen_port) {
  const auto listen_infos = llm::LLMUtils::Split(listen_info, ':');
  ADXL_CHK_BOOL_RET_STATUS(listen_infos.size() >= 1U, PARAM_INVALID,
                           "listen info is invalid: %s, expect ${ip}:${port} or ${ip}", listen_info.c_str());
  listen_ip = listen_infos[0];
  ADXL_CHK_LLM_RET(llm::LLMUtils::CheckIp(listen_ip), "IP is invalid: %s, listen info = %s",
                   listen_ip.c_str(), listen_info.c_str());
  if (listen_infos.size() > 1U) {
    ADXL_CHK_LLM_RET(llm::LLMUtils::ToNumber(listen_infos[1], listen_port), "Port:%s is invalid.",
                     listen_infos[1].c_str());
  }
  return SUCCESS;
}
template<typename T>
Status ChannelEvictor::Serialize(const T &msg, std::string &msg_str) {
   try {
    nlohmann::json j = msg;
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(PARAM_INVALID, "Failed to dump msg to str, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

template<typename T>
Status ChannelEvictor::SendMsg(int32_t fd, EvictChannelMsgType msg_type, const T &msg) {
  std::string msg_str;
  ADXL_CHK_STATUS_RET(ChannelEvictor::Serialize(msg, msg_str), "Failed to serialize msg");
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::SendMsg(fd, static_cast<int32_t>(msg_type), msg_str),
                   "Failed to send msg");
  return SUCCESS;
}

template<typename T>
Status ChannelEvictor::Deserialize(const std::vector<char> &msg_str, T &msg) {
   try {
    auto j = nlohmann::json::parse(&msg_str[0]);
    msg = j.get<T>();
  } catch (const nlohmann::json::exception &e) {
    LLMLOGE(PARAM_INVALID, "Failed to load msg, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

template<typename T>
Status ChannelEvictor::RecvMsg(int32_t fd, EvictChannelMsgType msg_type, T &msg) {
  std::vector<char> msg_str;
  int32_t type = 0;
  ADXL_CHK_LLM_RET(llm::MsgHandlerPlugin::RecvMsg(fd, type, msg_str),
                   "Failed to recv msg");
  ADXL_CHK_BOOL_RET_STATUS(msg_type == static_cast<EvictChannelMsgType>(type),
                           FAILED, "Failed to check recv msg type:%d, expect type:%d",
                           type, static_cast<int32_t>(msg_type));
  ADXL_CHK_STATUS_RET(ChannelEvictor::Deserialize(msg_str, msg), "Failed to deserialize msg");
  return SUCCESS;
}

static void from_json(const nlohmann::json &j, EvictChannelStatus &c) {
  j.at("error_code").get_to(c.error_code);
  j.at("error_message").get_to(c.error_message);
}

static void to_json(nlohmann::json &j, const EvictChannelDisconnectInfo &c) {
  j = nlohmann::json{};
  j["channel_id"] = c.channel_id;
}
}  // namespace adxl
