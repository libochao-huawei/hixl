/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "thread_pool.h"

namespace hixl {
ThreadPool::ThreadPool(std::string thread_name_prefix,
                       const uint32_t min_size,
                       const uint32_t max_size)
    : thread_name_prefix_(std::move(thread_name_prefix)),
      is_stoped_(false),
      min_thrd_num_(min_size < 1U ? 1U : min_size),
      max_thrd_num_(max_size < min_thrd_num_ ? min_thrd_num_ : max_size) {
  idle_thrd_num_ = min_thrd_num_;
  total_thrd_num_ = min_thrd_num_;

  for (uint32_t i = 0U; i < min_thrd_num_; ++i) {
    pool_.emplace_back(&ThreadFunc, this, i, false);
  }
  HIXL_LOGI("[ThreadPool] created, name:%s, min_size:%u, max_size:%u, core_threads:%u",
            thread_name_prefix_.c_str(), min_thrd_num_, max_thrd_num_, min_thrd_num_);
}

ThreadPool::~ThreadPool() {
  Destroy();
}

void ThreadPool::Destroy() {
  if (is_stoped_.load() == true) {
    return;
  }
  HIXL_LOGI("[ThreadPool] destroying, name:%s, total_threads:%u, pending_tasks:%zu",
            thread_name_prefix_.c_str(), total_thrd_num_.load(), tasks_.size());
  is_stoped_.store(true);
  {
    const std::unique_lock<std::mutex> lock{m_lock_};
    cond_var_.notify_all();
  }

  for (std::thread &thd : pool_) {
    if (thd.joinable()) {
      try {
        thd.join();
      } catch (...) {
        HIXL_LOGW("thread join exception");
      }
    }
  }
  HIXL_LOGI("[ThreadPool] destroyed, name:%s", thread_name_prefix_.c_str());
}

void ThreadPool::AddTemporaryThread() {
  const std::lock_guard<std::mutex> lock{m_lock_};
  if (total_thrd_num_.load() >= max_thrd_num_) {
    HIXL_LOGD("[ThreadPool] cannot add temp thread, total:%u >= max:%u",
              total_thrd_num_.load(), max_thrd_num_);
    return;
  }
  uint32_t thread_idx = total_thrd_num_.load();
  ++total_thrd_num_;
  std::thread temp_thread(&ThreadFunc, this, thread_idx, true);
  temp_thread.detach();
  HIXL_LOGI("[ThreadPool] add temp thread, name:%s, idx:%u, total:%u, max:%u",
            thread_name_prefix_.c_str(), thread_idx, total_thrd_num_.load(), max_thrd_num_);
}

void ThreadPool::ThreadFunc(ThreadPool *const thread_pool, uint32_t thread_idx, bool is_temporary) {
  if (thread_pool == nullptr) {
    return;
  }
  std::string thread_type = is_temporary ? "temp" : "core";
  if (!thread_pool->thread_name_prefix_.empty()) {
    auto thread_name = thread_pool->thread_name_prefix_ + "_" + thread_type + "_" + std::to_string(thread_idx);
    int32_t set_ret = pthread_setname_np(pthread_self(), thread_name.c_str());
    HIXL_LOGD("set thread name to [%s], ret=%d", thread_name.c_str(), set_ret);
  }
  HIXL_LOGD("[ThreadPool] thread started, type:%s, idx:%u", thread_type.c_str(), thread_idx);

  while (!thread_pool->is_stoped_) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock{thread_pool->m_lock_};
      thread_pool->cond_var_.wait(
          lock, [thread_pool]() -> bool {
            return thread_pool->is_stoped_.load() || (!thread_pool->tasks_.empty());
          });
      if (thread_pool->is_stoped_ && thread_pool->tasks_.empty()) {
        if (is_temporary) {
          --thread_pool->total_thrd_num_;
          HIXL_LOGI("[ThreadPool] temp thread exit on stop, idx:%u, total:%u",
                    thread_idx, thread_pool->total_thrd_num_.load());
        }
        return;
      }
      task = std::move(thread_pool->tasks_.front());
      thread_pool->tasks_.pop();
    }
    --thread_pool->idle_thrd_num_;
    task();

    if (is_temporary && !thread_pool->is_stoped_) {
      --thread_pool->total_thrd_num_;
      HIXL_LOGI("[ThreadPool] temp thread exit after task, idx:%u, total:%u, idle:%u",
                thread_idx, thread_pool->total_thrd_num_.load(), thread_pool->idle_thrd_num_.load());
      return;
    }
    ++thread_pool->idle_thrd_num_;
  }
}
}  // namespace hixl