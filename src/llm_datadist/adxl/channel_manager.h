/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_MANAGER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_MANAGER_H_

#include <map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <functional>
#include "channel.h"
#include "common/llm_mem_pool.h"
#include "buffer_transfer_service.h"

namespace adxl {
class ChannelManager {
 public:
  ChannelManager() = default;
  ~ChannelManager() = default;
  Status Initialize(BufferTransferService *buffer_transfer_service);
  Status Finalize();
  Status CreateChannel(const ChannelInfo &channel_info, ChannelPtr &channel_ptr);
  ChannelPtr GetChannel(ChannelType channel_type, const std::string &channel_id);
  Status DestroyChannel(ChannelType channel_type, const std::string &channel_id);
  static void SetHeartbeatWaitTime(int32_t time_in_millis);

  Status AddSocketToEpoll(int32_t fd, ChannelPtr channel);

  // 设置断链回调
  void SetDisconnectCallback(std::function<Status(const std::string&, int32_t)> callback) {
    disconnect_callback_ = callback;
  }
  
  // 设置断链响应回调
  void SetDisconnectResponseCallback(std::function<void(const RequestDisconnectResp&)> callback) {
    disconnect_response_callback_ = callback;
  }

  std::vector<ChannelPtr> GetAllClientChannel();
  std::vector<ChannelPtr> GetAllServerChannel();

 private:
  int max_channel = 512;

  void SendHeartbeats();
  void CheckHeartbeatTimeouts();

  Status HandleEpoolEvents();
  Status HandleSocketEvent(int32_t fd);
  Status HandleReadEvent(const ChannelPtr &channel);
  Status ProcessReceivedData(const ChannelPtr &channel);
  Status HandleControlMessage(const ChannelPtr &channel) const;
  Status RemoveFd(int32_t fd);

  // Handle specific control message types
  Status HandleHeartBeatMessage(const ChannelPtr &channel) const;
  Status HandleBufferReqMessage(const ChannelPtr &channel, const std::string &msg_str) const;
  Status HandleBufferRespMessage(const ChannelPtr &channel, const std::string &msg_str) const;
  Status HandleRequestDisconnectMessage(const ChannelPtr &channel, const std::string &msg_str) const;
  Status HandleRequestDisconnectRespMessage(const ChannelPtr &channel, const std::string &msg_str) const;

 private:
  // Helper functions for HandleRequestDisconnectMessage
  void HandleChannelIdMismatch(const ChannelPtr &channel, const RequestDisconnectMsg &req_msg) const;
  void InitDisconnectResponse(RequestDisconnectResp &resp, const RequestDisconnectMsg &req_msg) const;
  void HandleCanDisconnectCase(const ChannelPtr &channel, const RequestDisconnectMsg &req_msg, RequestDisconnectResp &resp) const;
  void HandleChannelBusyCase(const ChannelPtr &channel, const RequestDisconnectMsg &req_msg, RequestDisconnectResp &resp) const;
  void HandleCallbackNotSetCase(const ChannelPtr &channel, const RequestDisconnectMsg &req_msg, RequestDisconnectResp &resp) const;

  std::atomic<bool> stop_signal_{false};

  std::thread heartbeat_sender_;
  std::mutex cv_mutex_;
  std::condition_variable cv_;

  std::mutex mutex_;
  std::map<std::pair<ChannelType, std::string>, ChannelPtr> channels_;

  int epoll_fd_ = -1;
  static int64_t wait_time_in_millis_;
  BufferTransferService *buffer_transfer_service_ = nullptr;
  std::mutex fd_mutex_;
  std::map<int32_t, ChannelPtr> fd_to_channel_map_;

  std::thread msg_receiver_;
  rtContext_t rt_context_{nullptr};
  
  std::function<Status(const std::string&, int32_t)> disconnect_callback_;
  std::function<void(const RequestDisconnectResp&)> disconnect_response_callback_;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_MANAGER_H_
