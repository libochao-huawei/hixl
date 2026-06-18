/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMM_DESTROYER_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMM_DESTROYER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "adxl/adxl_types.h"
#include "hccl/hccl_adapter.h"

namespace adxl {
// Destroys HCCL comm domains asynchronously: each enqueued comm domain is destroyed only after a fixed
// delay (kCommDestroyDelayMillis). This lets the same engine pair re-establish a link (with a different
// comm name) while the previous comm domain is still pending destruction, instead of blocking the caller
// on the slow HcclCommDestroy.
class CommDestroyer {
 public:
  static constexpr int64_t kCommDestroyDelayMillis = 10000;

  CommDestroyer() = default;
  ~CommDestroyer();

  CommDestroyer(const CommDestroyer &) = delete;
  CommDestroyer &operator=(const CommDestroyer &) = delete;

  Status Initialize(aclrtContext ctx);

  // Hand a comm domain (and the memory handles bound to it) to the background thread for delayed
  // destruction. Takes ownership of the comm handle; the caller must drop its own reference afterwards.
  // comm_name is the hcclCommName of the domain, logged on enqueue/destroy for traceability.
  void Enqueue(HcclComm comm, const std::string &comm_name, std::vector<void *> bind_handles);

  // Block until every currently pending comm domain (queued or in-flight) has been destroyed, expediting
  // them so the caller does not wait out the remaining delay. Used before deregistering global memory that
  // may still be bound to a comm whose unbind is deferred.
  void DrainPending();

  // Stop the worker, destroy all remaining comm domains immediately (ignoring the delay), and join.
  void Finalize();

  // For tests only: override the destroy delay. Should be called before Enqueue.
  void SetDestroyDelay(int64_t delay_in_millis);

 private:
  struct PendingDestroy {
    HcclComm comm;
    std::string comm_name;
    std::vector<void *> bind_handles;
    std::chrono::steady_clock::time_point ready;
  };

  void Worker();
  // Destroy the front item with the lock held; releases the lock during the slow HCCL call.
  void DestroyFront(std::unique_lock<std::mutex> &lock);
  static void DestroyComm(const PendingDestroy &item);

  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable drained_cv_;
  std::deque<PendingDestroy> queue_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> initialized_{false};
  bool flush_{false};
  int32_t active_destroys_{0};
  int64_t delay_in_millis_{kCommDestroyDelayMillis};
  aclrtContext aclrt_context_{nullptr};
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_COMM_DESTROYER_H_
