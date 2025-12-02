/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_EVICTOR_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_EVICTOR_H_

#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <optional>
#include <chrono>
#include "adxl_utils.h"
#include "common/msg_handler_plugin.h"
#include "channel_manager.h"

namespace adxl {

enum class EvictChannelMsgType : int32_t {
  kConnect = 1,
  kDisconnect = 2,
  kStatus = 3,
  kEnd
};

struct EvictChannelStatus {
  uint32_t error_code;
  std::string error_message;
};

struct EvictChannelDisconnectInfo {
  std::string channel_id;
};

struct EvictItem {
    std::string channel_id;
    ChannelType channel_type;
    int32_t timeout_ms{1000};
};

struct PendingDisconnectRequest {
    std::condition_variable cv;
    bool received{false};
    RequestDisconnectResp resp;
};

struct ChannelState {
    ChannelPtr channel;
    std::string channel_id;
    int transfer_count;
    bool has_transferred;
    bool disconnect_flag;
};

class ChannelEvictor {
public:
    explicit ChannelEvictor(ChannelManager* channel_manager);
    ~ChannelEvictor() = default;

    Status Initialize(const std::map<AscendString, AscendString>& options);
    Status Finalize();

    int32_t GetTotalChannelCount() const;
    bool ShouldTriggerEviction() const;
    Status NotifyEviction();
    Status ProcessEviction(const EvictItem& item);
    Status ResetAllTransferFlags();
    Status Disconnect(const std::string &remote_engine, int32_t timeout_in_millis);
    Status SetListenInfo(const std::string listen_info);

private:
    void EvictionLoop();
    std::vector<EvictItem> SelectEvictionCandidates(int32_t need_expire);
    Status ParseMaxChannel(const std::map<AscendString, AscendString>& options);
    Status ParseHighWaterline(const std::map<AscendString, AscendString>& options);
    Status ParseLowWaterline(const std::map<AscendString, AscendString>& options);
    Status StartEvictionThread();
    Status SetupChannelManagerCallbacks();
    
    Status ProcessServerEviction(const std::string& channel_id, ChannelPtr channel);
    Status ProcessClientEviction(const std::string& channel_id, int32_t timeout_ms);

    Status DisconnectInfoProcess(ChannelType channel_type, const EvictChannelDisconnectInfo &peer_disconnect_info);
    template<typename T>
    static Status SendMsg(int32_t fd, EvictChannelMsgType msg_type, const T &msg);
    template<typename T>
    static Status RecvMsg(int32_t fd, EvictChannelMsgType msg_type, T &msg);
    template<typename T>
    static Status Serialize(const T &msg, std::string &msg_str);
    template<typename T>
    static Status Deserialize(const std::vector<char> &msg_str, T &msg);
    static Status ParseListenInfo(const std::string &listen_info, std::string &listen_ip, int32_t &listen_port);

    ChannelManager* channel_manager_;
    int32_t max_channel_{kDefaultMaxChannel};
    double high_waterline_ratio_{kDefaultHighWaterline};
    double low_waterline_ratio_{kDefaultLowWaterline};
    int32_t high_waterline_{0};
    int32_t low_waterline_{0};
    std::string listen_info_;

    std::mutex evict_mutex_;
    std::condition_variable evict_cv_;
    std::queue<EvictItem> evict_queue_;
    std::atomic<bool> start_eviction_{false};
    std::atomic<bool> stop_eviction_{false};
    std::thread eviction_thread_;
    std::mutex pending_req_mutex_;
    std::map<uint64_t, std::shared_ptr<PendingDisconnectRequest>> pending_disconnect_requests_;
    std::atomic<uint64_t> next_req_id_{1};
};

}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_EVICTOR_H_
