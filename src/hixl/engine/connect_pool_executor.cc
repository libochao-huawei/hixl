/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "connect_pool_executor.h"
#include "common/hixl_checker.h"
#include "nlohmann/json.hpp"

namespace hixl {
namespace {
constexpr const int32_t OPTION_CONNECT_POOL_THREAD_NUM = 2;
constexpr const int32_t OPTION_CONNECT_POOL_TASK_QUEUE_CAPACITY = 128;

constexpr const int32_t LIMIT_THREAD_NUM_MIN = 1;
constexpr const int32_t LIMIT_THREAD_NUM_MAX = 64;
constexpr const int32_t LIMIT_TASK_QUEUE_CAPACITY_MIN = 1;
constexpr const int32_t LIMIT_TASK_QUEUE_CAPACITY_MAX = 65535;
}  // namespace

ConnectPoolExecutor::ConnectPoolExecutor() {}

ConnectPoolExecutor::~ConnectPoolExecutor() {
  if (is_initialized_.load(std::memory_order::memory_order_relaxed)) {
    Shutdown();
  }
}

Status ConnectPoolExecutor::Initialize(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("ConnectPoolExecutor initialize start");
  HIXL_CHK_BOOL_RET_STATUS(!IsInitialized(), SUCCESS, "ConnectPoolExecutor is already initialized");
  const auto ret = ParseGlobalResourceConfig(options);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to parse global resource config");
  HIXL_CHK_BOOL_RET_STATUS(thread_num_ >= LIMIT_THREAD_NUM_MIN && thread_num_ <= LIMIT_THREAD_NUM_MAX, PARAM_INVALID,
                           "thread_num:%d must in [%d, %d]", thread_num_, LIMIT_THREAD_NUM_MIN, LIMIT_THREAD_NUM_MAX);
  HIXL_CHK_BOOL_RET_STATUS(
      task_queue_capacity_ >= LIMIT_TASK_QUEUE_CAPACITY_MIN && task_queue_capacity_ <= LIMIT_TASK_QUEUE_CAPACITY_MAX,
      PARAM_INVALID, "task_queue_capacity:%d must in [%d, %d]", thread_num_, LIMIT_TASK_QUEUE_CAPACITY_MIN,
      LIMIT_TASK_QUEUE_CAPACITY_MAX);

  (void)aclrtGetCurrentContext(&ctx_);
  for (int32_t i = 0; i < thread_num_; ++i) {
    workers_.emplace_back([this, i]() { WorkerHandler(i); });
  }
  is_initialized_.store(true, std::memory_order::memory_order_relaxed);
  HIXL_LOGI("ConnectPoolExecutor initialize success");
  return SUCCESS;
}

void ConnectPoolExecutor::Shutdown() {
  HIXL_LOGI("ConnectPoolExecutor shutdown start");
  if (!is_initialized_.load(std::memory_order::memory_order_relaxed)) {
    HIXL_LOGW("ConnectPoolExecutor is already shutdown");
    return;
  }

  is_initialized_.store(false, std::memory_order::memory_order_relaxed);
  task_queue_cv_.notify_all();
  HIXL_LOGI("ConnectPoolExecutor wait worker exit");
  for (auto &worker : workers_) {
    worker.join();
  }
  workers_.clear();
  HIXL_LOGI("ConnectPoolExecutor shutdown success");
}

Status ConnectPoolExecutor::Submit(const std::function<void()> &task, const AscendString &remote_engine,
                                   const bool is_connect) {
  std::unique_lock<std::mutex> lock(task_queue_mutex_);
  if (task_list_.size() >= static_cast<std::size_t>(task_queue_capacity_)) {
    HIXL_LOGW("task_queue is full, task_queue_capacity:%d", task_queue_capacity_);
    return RESOURCE_EXHAUSTED;
  }
  auto it = task_map_.find(remote_engine);
  if (it != task_map_.end()) {
    if (it->second->is_connect == is_connect) {
      // 任务队列中已有相同任务，则忽略新任务
      HIXL_LOGW("ignore task %s to %s", is_connect ? "connect" : "disconnect", remote_engine.GetString());
      return SUCCESS;
    }

    // 任务队列中已有不同任务，则删除已有任务
    HIXL_LOGW("remove task %s to %s", it->second->is_connect ? "connect" : "disconnect", remote_engine.GetString());
    task_list_.erase(it->second);
    task_map_.erase(remote_engine);
  }

  // 新任务插入队尾
  task_list_.emplace_back(ConnectPoolExecutorTask{is_connect, remote_engine, task});
  task_map_[remote_engine] = std::prev(task_list_.end());
  HIXL_LOGI("submit task %s to %s", is_connect ? "connect" : "disconnect", remote_engine.GetString());

  task_queue_cv_.notify_one();
  return SUCCESS;
}

void ConnectPoolExecutor::SetStatus(const AscendString &remote_engine, const AsyncConnectStatus status) {
  std::unique_lock<std::mutex> lock(task_result_mutex_);
  if (status == AsyncConnectStatus::NOT_CONNECT) {
    task_result_.erase(remote_engine);
  } else {
    task_result_[remote_engine] = status;
  }
}

Status ConnectPoolExecutor::GetStatus(const AscendString &remote_engine, AsyncConnectStatus &status) {
  std::unique_lock<std::mutex> lock(task_result_mutex_);
  const auto &it = task_result_.find(remote_engine);
  status = (it == task_result_.end()) ? AsyncConnectStatus::NOT_CONNECT : it->second;
  return SUCCESS;
}

Status ConnectPoolExecutor::GetStatus(std::map<AscendString, AsyncConnectStatus> &statuses) {
  statuses.clear();
  std::unique_lock<std::mutex> lock(task_result_mutex_);
  std::copy(task_result_.begin(), task_result_.end(), std::inserter(statuses, statuses.begin()));
  return SUCCESS;
}

bool ConnectPoolExecutor::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

Status ConnectPoolExecutor::ParseGlobalResourceConfig(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("ParseGlobalResourceConfig start");
  thread_num_ = hixl::OPTION_CONNECT_POOL_THREAD_NUM;
  task_queue_capacity_ = hixl::OPTION_CONNECT_POOL_TASK_QUEUE_CAPACITY;

  const auto &it = options.find(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  if (it == options.end()) {
    HIXL_LOGW("Failed to find %s, use default thread_num:%d task_queue_capacity:%d",
              hixl::OPTION_GLOBAL_RESOURCE_CONFIG, thread_num_, task_queue_capacity_);
    return SUCCESS;
  }

  std::string config_str = it->second.GetString();
  if (config_str.empty()) {
    HIXL_LOGW("Failed to parse empty %s, use default thread_num:%d task_queue_capacity:%d",
              hixl::OPTION_GLOBAL_RESOURCE_CONFIG, thread_num_, task_queue_capacity_);
    return SUCCESS;
  }

  try {
    auto config = nlohmann::json::parse(config_str);
    std::string thread_num = config["connect_pool.thread_num"];
    std::string task_queue_capacity = config["connect_pool.task_queue_capacity"];
    thread_num_ = std::stoi(thread_num);
    task_queue_capacity_ = std::stoi(task_queue_capacity);
  } catch (const std::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse %s error %s", hixl::OPTION_GLOBAL_RESOURCE_CONFIG, e.what());
    return PARAM_INVALID;
  }
  HIXL_LOGI("ParseGlobalResourceConfig success, thread_num:%d task_queue_capacity:%d", thread_num_,
            task_queue_capacity_);
  return SUCCESS;
}

void ConnectPoolExecutor::WorkerHandler(const int32_t workerId) {
  HIXL_LOGI("ConnectPoolExecutor worker %d start", workerId);
  HIXL_CHK_ACL(aclrtSetCurrentContext(ctx_));
  while (true) {
    ConnectPoolExecutorTask executor_task{false, "", nullptr};
    {
      std::unique_lock<std::mutex> lock(task_queue_mutex_);
      task_queue_cv_.wait(lock, [this]() {
        return !is_initialized_.load(std::memory_order::memory_order_relaxed) || !task_list_.empty();
      });

      if (!is_initialized_.load(std::memory_order::memory_order_relaxed)) {
        HIXL_LOGW("ConnectPoolExecutor is shutdown, %llu task remain", task_list_.size());
        break;
      }

      if (task_list_.empty()) {
        continue;
      }

      for (auto it = task_list_.begin(); it != task_list_.end(); ++it) {
        AsyncConnectStatus status;
        GetStatus(it->remote_engine, status);
        if (status != AsyncConnectStatus::CONNECTING && status != AsyncConnectStatus::DISCONNECTING) {
          SetStatus(it->remote_engine,
                    it->is_connect ? AsyncConnectStatus::CONNECTING : AsyncConnectStatus::DISCONNECTING);
          executor_task = std::move(*it);
          task_map_.erase(executor_task.remote_engine);
          task_list_.erase(it);
          break;
        }
      }
    }

    if (executor_task.task != nullptr) {
      HIXL_LOGI("worker %d exec task %s to %s start", workerId, executor_task.is_connect ? "connect" : "disconnect",
                executor_task.remote_engine.GetString());
      executor_task.task();
      HIXL_LOGI("worker %d exec task %s to %s success", workerId, executor_task.is_connect ? "connect" : "disconnect",
                executor_task.remote_engine.GetString());
    }
  }
  HIXL_LOGI("ConnectPoolExecutor worker %d exit", workerId);
}
}  // namespace hixl