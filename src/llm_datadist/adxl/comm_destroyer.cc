/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_destroyer.h"

#include <utility>
#include "common/llm_log.h"

namespace adxl {
CommDestroyer::~CommDestroyer() {
  Finalize();
}

Status CommDestroyer::Initialize(aclrtContext ctx) {
  aclrt_context_ = ctx;
  stop_.store(false);
  worker_ = std::thread([this]() { Worker(); });
  initialized_ = true;
  return SUCCESS;
}

void CommDestroyer::SetDestroyDelay(int64_t delay_in_millis) {
  std::lock_guard<std::mutex> lock(mutex_);
  delay_in_millis_ = delay_in_millis;
}

void CommDestroyer::Enqueue(HcclComm comm, const std::string &comm_name, std::vector<void *> bind_handles) {
  if (comm == nullptr) {
    return;
  }
  int64_t delay = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    delay = delay_in_millis_;
    PendingDestroy item{};
    item.comm = comm;
    item.comm_name = comm_name;
    item.bind_handles = std::move(bind_handles);
    item.ready = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
    queue_.push_back(std::move(item));
  }
  cv_.notify_all();
  LLMLOGI("Enqueue comm domain %p, comm_name:%s for async destroy after %ld ms.", comm, comm_name.c_str(), delay);
}

void CommDestroyer::Finalize() {
  if (!initialized_) {
    return;
  }
  stop_.store(true);
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  initialized_ = false;
}

void CommDestroyer::DestroyComm(const PendingDestroy &item) {
  LLMLOGI("Async destroy comm domain begin, comm:%p, comm_name:%s.", item.comm, item.comm_name.c_str());
  for (auto handle : item.bind_handles) {
    auto unbind_ret = llm::HcclAdapter::GetInstance().HcclCommUnbindMem(item.comm, handle);
    if (unbind_ret != HcclResult::HCCL_SUCCESS) {
      LLMLOGE(FAILED, "Async HcclCommUnbindMem failed, comm:%p, comm_name:%s, ret:%d.", item.comm,
              item.comm_name.c_str(), static_cast<int32_t>(unbind_ret));
    }
  }
  auto destroy_ret = llm::HcclAdapter::GetInstance().HcclCommDestroy(item.comm);
  if (destroy_ret != HcclResult::HCCL_SUCCESS) {
    LLMLOGE(FAILED, "Async HcclCommDestroy failed, comm:%p, comm_name:%s, ret:%d.", item.comm, item.comm_name.c_str(),
            static_cast<int32_t>(destroy_ret));
    return;
  }
  LLMLOGI("Async HcclCommDestroy success, comm:%p, comm_name:%s.", item.comm, item.comm_name.c_str());
}

void CommDestroyer::Worker() {
  (void)aclrtSetCurrentContext(aclrt_context_);
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stop_.load()) {
    if (queue_.empty()) {
      cv_.wait(lock, [this]() { return stop_.load() || !queue_.empty(); });
      continue;
    }
    const auto ready = queue_.front().ready;
    if (!flush_ && std::chrono::steady_clock::now() < ready) {
      // All items share the same delay, so the front is always the earliest-ready one.
      (void)cv_.wait_until(lock, ready);
      continue;
    }
    DestroyFront(lock);
  }
  // Stop requested: destroy everything left immediately, ignoring the remaining delay.
  while (!queue_.empty()) {
    DestroyFront(lock);
  }
  flush_ = false;
  drained_cv_.notify_all();
}

void CommDestroyer::DestroyFront(std::unique_lock<std::mutex> &lock) {
  PendingDestroy item = std::move(queue_.front());
  queue_.pop_front();
  ++active_destroys_;
  lock.unlock();
  DestroyComm(item);
  lock.lock();
  --active_destroys_;
  if (queue_.empty() && active_destroys_ == 0) {
    flush_ = false;
    drained_cv_.notify_all();
  }
}

void CommDestroyer::DrainPending() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!initialized_ || (queue_.empty() && active_destroys_ == 0)) {
    return;
  }
  // Expedite the pending destroys instead of waiting out the delay, then block until they all finish.
  flush_ = true;
  cv_.notify_all();
  drained_cv_.wait(lock, [this]() { return queue_.empty() && active_destroys_ == 0; });
}
}  // namespace adxl
