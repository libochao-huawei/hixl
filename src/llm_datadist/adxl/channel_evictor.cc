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
#include <common/llm_utils.h>
#include <adxl/adxl_types.h>
#include "channel_msg_handler.h"
#include "adxl_utils.h"

namespace adxl {

ChannelEvictor::ChannelEvictor(ChannelManager* channel_manager, ChannelMsgHandler* msg_handler)
  : channel_manager_(channel_manager),
    msg_handler_(msg_handler) {}

Status ChannelEvictor::Initialize(const std::map<AscendString, AscendString>& options) {
  ADXL_CHECK_NOTNULL(channel_manager_);
  ADXL_CHECK_NOTNULL(msg_handler_);

  // 解析水位线配置
  ParseMaxChannel(options);
  ParseHighWaterline(options);
  ParseLowWaterline(options);
  // 验证配置并计算实际值
  ADXL_CHK_BOOL_RET_STATUS(low_waterline_ratio_ < high_waterline_ratio_, PARAM_INVALID,
               "low_waterline (%.2f) must be less than high_waterline (%.2f)",
               low_waterline_ratio_, high_waterline_ratio_);
  
  high_waterline_ = static_cast<int>(max_channel_ * high_waterline_ratio_);
  low_waterline_ = static_cast<int>(max_channel_ * low_waterline_ratio_);
  
  high_waterline_ = std::max(1, high_waterline_);
  low_waterline_ = std::max(1, low_waterline_);
  
  ADXL_CHK_BOOL_RET_STATUS(low_waterline_ <= high_waterline_, PARAM_INVALID,
               "low_waterline (%d) must be less than high_waterline (%d)",
               low_waterline_, high_waterline_);
  ADXL_CHK_BOOL_RET_STATUS(high_waterline_ <= max_channel_, PARAM_INVALID,
               "high_waterline (%d) must be less than or equal to max_channel (%d)",
               high_waterline_, max_channel_);
  
  LLMLOGI("Waterline config: max_channel=%d, high_waterline=%.2f (%d), low_waterline=%.2f (%d)",
      max_channel_, high_waterline_ratio_, high_waterline_, 
      low_waterline_ratio_, low_waterline_);
  
  // 开始初始化
  StartEvictionThread();
  
  // 设置断链回调给ChannelManager
  SetupChannelManagerCallbacks();
  
  return SUCCESS;
}

void ChannelEvictor::ParseMaxChannel(const std::map<AscendString, AscendString>& options) {
  auto max_it = options.find(OPTION_MAX_CHANNEL);
  if (max_it != options.end()) {
    std::string max_str = max_it->second.GetString();
    if (llm::LLMUtils::ToNumber(max_str, max_channel_)) {
      LLMLOGW("Invalid max_channel: %s, use default: %d", max_str.c_str(), kDefaultMaxChannel);
      max_channel_ = kDefaultMaxChannel;
    }
    if (max_channel_ <= 0) {
      LLMLOGW("Invalid max_channel: %d, use default: %d", max_channel_, kDefaultMaxChannel);
      max_channel_ = kDefaultMaxChannel;
    }
  }
}

void ChannelEvictor::ParseHighWaterline(const std::map<AscendString, AscendString>& options) {
  auto high_it = options.find(OPTION_HIGH_WATERLINE);
  if (high_it != options.end()) {
    std::string high_str = high_it->second.GetString();
    double high_value = 0.0;
    if (llm::LLMUtils::ToNumber(high_str, high_value)) {
      LLMLOGW("Invalid high_waterline: %s, use default: %.2f", 
          high_str.c_str(), kDefaultHighWaterline);
      high_waterline_ratio_ = kDefaultHighWaterline;
    } else {
      if (high_value > 0.0 && high_value <= 1.0) {
        high_waterline_ratio_ = high_value;
      } else {
        LLMLOGW("Invalid high_waterline: %.2f (must be 0~1), use default: %.2f", 
            high_value, kDefaultHighWaterline);
        high_waterline_ratio_ = kDefaultHighWaterline;
      }
    }
  } else {
    high_waterline_ratio_ = kDefaultHighWaterline;
  }
}

void ChannelEvictor::ParseLowWaterline(const std::map<AscendString, AscendString>& options) {
  auto low_it = options.find(OPTION_LOW_WATERLINE);
  if (low_it != options.end()) {
    std::string low_str = low_it->second.GetString();
    double low_value = 0.0;
    if (llm::LLMUtils::ToNumber(low_str, low_value)) {
      LLMLOGW("Invalid low_waterline: %s, use default: %.2f", 
          low_str.c_str(), kDefaultLowWaterline);
      low_waterline_ratio_ = kDefaultLowWaterline;
    } else {
      if (low_value > 0.0 && low_value <= 1.0) {
        low_waterline_ratio_ = low_value;
      } else {
        LLMLOGW("Invalid low_waterline: %.2f (must be 0~1), use default: %.2f", 
            low_value, kDefaultLowWaterline);
        low_waterline_ratio_ = kDefaultLowWaterline;
      }
    }
  } else {
    low_waterline_ratio_ = kDefaultLowWaterline;
  }
}

void ChannelEvictor::StartEvictionThread() {
  if (high_waterline_ > 0 && low_waterline_ > 0) {
    stop_eviction_ = false;
    eviction_thread_ = std::thread([this]() { 
      EvictionLoop(); 
    });
    LLMLOGI("Eviction thread started with waterline: max=%d, high=%d, low=%d", 
        max_channel_, high_waterline_, low_waterline_);
  }
}

void ChannelEvictor::SetupChannelManagerCallbacks() {
  // 设置断链回调
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
    item.task_type = EvictTaskType::DISCONNECT_CHANNEL;
    item.timeout_ms = timeout_ms;
    {
        std::lock_guard<std::mutex> lock(evict_mutex_);
        evict_queue_.push(item);
        pending_evictions_++;
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
}

void ChannelEvictor::Finalize() {
  if (eviction_thread_.joinable()) {
    {
      std::lock_guard<std::mutex> lock(evict_mutex_);
      stop_eviction_ = true;
    }
    evict_cv_.notify_all();
    eviction_thread_.join();
  }
}

int ChannelEvictor::GetTotalChannelCount() const {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  return static_cast<int>(client_channels.size() + server_channels.size());
}

bool ChannelEvictor::ShouldTriggerEviction() const {
  return GetTotalChannelCount() >= high_waterline_;
}

bool ChannelEvictor::ShouldStopEviction() const {
  return GetTotalChannelCount() <= low_waterline_;
}

void ChannelEvictor::MaybeScheduleEviction() {
  int current_count = GetTotalChannelCount();
  
  int need_expire = current_count - low_waterline_;
  if (need_expire <= 0) {
    return;
  }
  
  std::optional<EvictItem> candidate = SelectOneEvictionCandidate();
  
  if (!candidate.has_value()) {
    LLMLOGW("No candidate for eviction, current channels=%d", current_count);
    return;
  }
  
  {
    std::lock_guard<std::mutex> lock(evict_mutex_);
    evict_queue_.push(candidate.value());
    auto channel = channel_manager_->GetChannel(candidate->channel_type, candidate->channel_id);
    if (channel != nullptr) {
        channel->SetDisconnecting(true);
    }
    pending_evictions_++;
    LLMLOGI("Scheduled 1 eviction, pending=%d", pending_evictions_);
  }
  
  evict_cv_.notify_one();
}

std::optional<EvictItem> ChannelEvictor::SelectOneEvictionCandidate() {
  auto client_channels = channel_manager_->GetAllClientChannel();
  auto server_channels = channel_manager_->GetAllServerChannel();
  
  std::vector<ChannelPtr>& target_channels = client_channels.size() >= server_channels.size() 
      ? client_channels : server_channels;
  ChannelType target_type = client_channels.size() >= server_channels.size() 
      ? ChannelType::kClient : ChannelType::kServer;

  std::vector<ChannelState> states = CreateChannelStates(target_channels);

  SortChannelStates(states);

  return FindFirstEligibleChannel(states, target_type);
}

std::vector<ChannelEvictor::ChannelState> ChannelEvictor::CreateChannelStates(const std::vector<ChannelPtr>& channels) {
  std::vector<ChannelState> states;
  states.reserve(channels.size());
  
  for (const auto& channel : channels) {
    states.push_back({
      channel,
      channel->GetChannelId(),
      channel->GetTransferCount(),
      channel->GetHasTransferred(),
      channel->IsDisconnecting()
    });
  }
  return states;
}

void ChannelEvictor::SortChannelStates(std::vector<ChannelState>& states) {
  // Sort: 1. has_transferred==false first, 2. maintain creation order
  std::sort(states.begin(), states.end(), 
        [](const ChannelState& a, const ChannelState& b) {
          if (a.has_transferred != b.has_transferred) {
            return !a.has_transferred;
          }
          return false;
        });
}

std::optional<EvictItem> ChannelEvictor::FindFirstEligibleChannel(
    const std::vector<ChannelState>& states, ChannelType target_type) {
  for (const auto& state : states) {
    if (state.transfer_count > 0 || state.disconnect_flag) {
      continue;
    }
    
    EvictItem item;
    item.channel_id = state.channel_id;
    item.channel_type = target_type;
    return item;
  }
  
  return std::nullopt;  // no eligible candidate
}

void ChannelEvictor::EvictionLoop() {
  while (true) {
    std::unique_lock<std::mutex> lock(evict_mutex_);
    
    evict_cv_.wait(lock, [this] { 
      return !evict_queue_.empty() || stop_eviction_; 
    });
    
    if (stop_eviction_) {
      break;
    }
    
    if (evict_queue_.empty()) {
      continue;
    }
    
    EvictItem item = evict_queue_.front();
    evict_queue_.pop();
    pending_evictions_--;
    
    lock.unlock();
    
    bool has_evicted = false;
    if (item.task_type == EvictTaskType::EVICT_CHANNEL) {
      has_evicted = ProcessEviction(item);
      if (!ShouldStopEviction()) {
        MaybeScheduleEviction();
      } else {
        ResetAllTransferFlags();
      }
    } else if (item.task_type == EvictTaskType::DISCONNECT_CHANNEL) {
      has_evicted = ProcessDisconnectChannelTask(item);
    }
  }
}

bool ChannelEvictor::ProcessDisconnectChannelTask(const EvictItem& item) {
  auto channel = channel_manager_->GetChannel(item.channel_type, item.channel_id);
  if (channel == nullptr) {
    LLMLOGW("Channel %s not found for disconnect", item.channel_id.c_str());
    return false;
  }
  
  Status ret = msg_handler_->Disconnect(item.channel_id, item.timeout_ms);
  bool has_evicted = (ret == SUCCESS);
  if (has_evicted) {
    LLMLOGI("Successfully disconnected channel %s by request", item.channel_id.c_str());
  } else {
    LLMLOGW("Failed to disconnect channel %s by request", item.channel_id.c_str());
  }
  return has_evicted;
}

bool ChannelEvictor::ProcessEviction(const EvictItem& item) {
  ADXL_CHECK_NOTNULL(msg_handler_);
  auto channel = channel_manager_->GetChannel(item.channel_type, item.channel_id);
  if (channel == nullptr) {
    return false;
  }
  if (channel->GetTransferCount() > 0) {
    return false;
  }
  if (item.channel_type == ChannelType::kServer) {
    return ProcessServerEviction(item.channel_id, channel);
  } else {
    return ProcessClientEviction(item.channel_id, item.timeout_ms);
  }
}

bool ChannelEvictor::ProcessServerEviction(const std::string& channel_id, ChannelPtr channel) {
  int32_t fd = channel->GetFd();
  if (fd < 0) {
    LLMLOGW("Channel %s has invalid fd, cannot send request disconnect", channel_id.c_str());
    return false;
  }
  uint64_t req_id = next_req_id_.fetch_add(1ULL, std::memory_order_acq_rel);
  auto pending_req = CreatePendingDisconnectRequest(req_id);
  if (!pending_req) {
    return false;
  }
  if (!SendDisconnectRequest(channel, req_id)) {
    RemovePendingDisconnectRequest(req_id);
    return false;
  }
  RequestDisconnectResp resp;
  bool received = WaitForDisconnectResponse(req_id, resp);
  RemovePendingDisconnectRequest(req_id);
  return ProcessDisconnectResponse(channel_id, resp, received);
}

std::shared_ptr<ChannelEvictor::PendingDisconnectRequest> ChannelEvictor::CreatePendingDisconnectRequest(uint64_t req_id) {
  auto pending_req = std::make_shared<PendingDisconnectRequest>();
  std::lock_guard<std::mutex> lock(pending_req_mutex_);
  pending_disconnect_requests_[req_id] = pending_req;
  return pending_req;
}

bool ChannelEvictor::SendDisconnectRequest(ChannelPtr channel, uint64_t req_id) {
  RequestDisconnectMsg req_msg;
  req_msg.channel_id = msg_handler_->GetListenInfo();
  req_msg.timeout = 1000ULL;
  req_msg.req_id = req_id;
  Status ret = channel->SendControlMsg([&req_msg](int32_t fd) {
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kRequestDisconnect, req_msg, req_msg.timeout);
  });
  if (ret != SUCCESS) {
    return false;
  }
  return true;
}

void ChannelEvictor::RemovePendingDisconnectRequest(uint64_t req_id) {
  std::lock_guard<std::mutex> lock(pending_req_mutex_);
  pending_disconnect_requests_.erase(req_id);
}

bool ChannelEvictor::WaitForDisconnectResponse(uint64_t req_id, RequestDisconnectResp& resp) {
  auto it = pending_disconnect_requests_.find(req_id);
  if (it == pending_disconnect_requests_.end()) {
    return false;
  }

  std::unique_lock<std::mutex> lock(pending_req_mutex_);
  bool received = it->second->cv.wait_for(lock, std::chrono::milliseconds(2000), [&]() {
    return it->second->received;
  });

  if (received) {
    resp = it->second->resp;
  }
  return received;
}

bool ChannelEvictor::ProcessDisconnectResponse(const std::string& channel_id, const RequestDisconnectResp& resp, bool received) {
  if (!received) {
    LLMLOGW("Timeout waiting for disconnect response, channel: %s", channel_id.c_str());
    return false;
  }
  
  if (resp.disconnected) {
    LLMLOGI("Successfully disconnected channel %s by server request", channel_id.c_str());
    return true;
  } else {
    LLMLOGW("Client refused or failed to disconnect channel %s, error_code=%u, error_message=%s", 
        channel_id.c_str(), resp.error_code, resp.error_message.c_str());
    return false;
  }
}

bool ChannelEvictor::ProcessClientEviction(const std::string& channel_id, int32_t timeout_ms) {
  Status ret = msg_handler_->Disconnect(channel_id, timeout_ms);
  if (ret == SUCCESS) {
    LLMLOGI("Evicted client channel: %s", channel_id.c_str());
    return true;
  } else {
    LLMLOGW("Failed to evict client channel: %s, ret=%d", channel_id.c_str(), ret);
    return false;
  }
}

void ChannelEvictor::ResetAllTransferFlags() {
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
}

}  // namespace adxl
