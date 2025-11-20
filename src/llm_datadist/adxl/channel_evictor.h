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
#include "channel_manager.h"

namespace adxl {

class ChannelMsgHandler;

enum class EvictTaskType {
    EVICT_CHANNEL,
    DISCONNECT_CHANNEL
};

struct EvictItem {
    std::string channel_id;
    ChannelType channel_type;
    EvictTaskType task_type{EvictTaskType::EVICT_CHANNEL};
    int32_t timeout_ms{1000};
};

struct PendingDisconnectRequest {
    std::condition_variable cv;
    bool received{false};
    RequestDisconnectResp resp;
};

class ChannelEvictor {
public:
    explicit ChannelEvictor(ChannelManager* channel_manager, ChannelMsgHandler* msg_handler);
    ~ChannelEvictor() = default;

    Status Initialize(const std::map<AscendString, AscendString>& options);
    void Finalize();

    int GetTotalChannelCount() const;
    bool ShouldTriggerEviction() const;
    bool ShouldStopEviction() const;
    void MaybeScheduleEviction();  // 每次只选择一个候选入队
    bool ProcessEviction(const EvictItem& item);  // 返回是否成功淘汰
    void ResetAllTransferFlags();

    
private:
    void EvictionLoop();
    std::optional<EvictItem> SelectOneEvictionCandidate();  // 每次只选择一个候选

    ChannelManager* channel_manager_;
    ChannelMsgHandler* msg_handler_;

    int max_channel_{kDefaultMaxChannel};
    double high_waterline_ratio_{kDefaultHighWaterline};
    double low_waterline_ratio_{kDefaultLowWaterline};
    int high_waterline_{0};
    int low_waterline_{0};

    // 淘汰队列和线程
    std::mutex evict_mutex_;
    std::condition_variable evict_cv_;
    std::queue<EvictItem> evict_queue_;
    int pending_evictions_{0};
    std::atomic<bool> stop_eviction_{false};
    std::thread eviction_thread_;

    // Server等待Client断链响应
    std::mutex pending_req_mutex_;
    std::map<uint64_t, std::shared_ptr<PendingDisconnectRequest>> pending_disconnect_requests_;
    std::atomic<uint64_t> next_req_id_{1};
};

}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_CHANNEL_EVICTOR_H_
