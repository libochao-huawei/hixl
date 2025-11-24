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

  // 设置断链响应回调
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
  
  std::vector<ChannelPtr>* target_channels = nullptr;
  ChannelType target_type;
  if (client_channels.size() >= server_channels.size()) {
    target_channels = &client_channels;
    target_type = ChannelType::kClient;
  } else {
    target_channels = &server_channels;
    target_type = ChannelType::kServer;
  }
  
  struct ChannelState {
    ChannelPtr channel;
    std::string channel_id;
    int transfer_count;
    bool has_transferred;
    bool disconnect_flag;
  };
  
  std::vector<ChannelState> states;
  states.reserve(target_channels->size());
  
  for (const auto& channel : *target_channels) {
    ChannelState state;
    state.channel = channel;
    state.channel_id = channel->GetChannelId();
    state.transfer_count = channel->GetTransferCount();
    state.has_transferred = channel->GetHasTransferred();
    state.disconnect_flag = channel->IsDisconnecting();
    states.push_back(state);
  }
  
  // 排序：1. has_transferred==false优先，2. 按建链顺序（已按顺序）
  std::sort(states.begin(), states.end(), 
        [](const ChannelState& a, const ChannelState& b) {
          if (a.has_transferred != b.has_transferred) {
            return !a.has_transferred;  // false排在前面
          }
          return false;
        });
  
  for (const auto& state : states) {
    if (state.transfer_count > 0 || state.disconnect_flag) {
      continue;
    }
    
    EvictItem item;
    item.channel_id = state.channel_id;
    item.channel_type = target_type;
    return item;
  }
  
  return std::nullopt;  // 没有可用候选
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

    // 根据任务类型执行不同操作
    if (item.task_type == EvictTaskType::EVICT_CHANNEL) {
      has_evicted = ProcessEviction(item);
      
      // 重新检查水位线，如果还需要淘汰，继续选择候选
      if (!ShouldStopEviction()) {
        MaybeScheduleEviction();  // 重新选择候选并入队
      } else {
        ResetAllTransferFlags();
      }
    } else if (item.task_type == EvictTaskType::DISCONNECT_CHANNEL) {
      auto channel = channel_manager_->GetChannel(item.channel_type, item.channel_id);
      if (channel == nullptr) {
        LLMLOGW("Channel %s not found for disconnect", item.channel_id.c_str());
      } else {
        Status ret = msg_handler_->Disconnect(item.channel_id, item.timeout_ms);
        has_evicted = (ret == SUCCESS);
      }
    }
  }
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
  // Server端：发送kRequestDisconnect消息给Client，等待Client响应
  int32_t fd = channel->GetFd();
  if (fd < 0) {
    LLMLOGW("Channel %s has invalid fd, cannot send request disconnect", channel_id.c_str());
    return false;
  }
  // 生成请求ID
  uint64_t req_id = next_req_id_.fetch_add(1ULL, std::memory_order_acq_rel);
  // 创建pending请求
  auto pending_req = std::make_shared<PendingDisconnectRequest>();
  {
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    pending_disconnect_requests_[req_id] = pending_req;
  }
  RequestDisconnectMsg req_msg;
  req_msg.channel_id = msg_handler_->GetListenInfo();
  req_msg.timeout = 1000ULL;
  req_msg.req_id = req_id;
  Status ret = channel->SendControlMsg([&req_msg](int32_t fd) {
    return ControlMsgHandler::SendMsg(fd, ControlMsgType::kRequestDisconnect, req_msg, req_msg.timeout);
  });
  if (ret != SUCCESS) {
    LLMLOGW("Failed to send request disconnect for channel: %s, ret=%d", channel_id.c_str(), ret);
    std::lock_guard<std::mutex> lock(pending_req_mutex_);
    pending_disconnect_requests_.erase(req_id);
    return false;
  }
  LLMLOGI("Sent request disconnect to client for channel: %s, req_id=%lu", channel_id.c_str(), req_id);
  // 等待响应
  std::unique_lock<std::mutex> lock(pending_req_mutex_);
  bool received = pending_req->cv.wait_for(lock, std::chrono::milliseconds(2000), [&pending_req] {
    return pending_req->received;
  });
  if (!received) {
    LLMLOGW("Timeout waiting for disconnect response, channel: %s, req_id=%lu", channel_id.c_str(), req_id);
    pending_disconnect_requests_.erase(req_id);
    return false;
  }
  // 获取响应
  RequestDisconnectResp resp = pending_req->resp;
  pending_disconnect_requests_.erase(req_id);
  lock.unlock();
  // 检查响应
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
