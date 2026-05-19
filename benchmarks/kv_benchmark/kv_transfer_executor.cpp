/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kv_transfer_executor.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <sys/syscall.h>
#include <utility>

namespace hixl_kv_benchmark {
namespace {

using hixl::AscendString;
using hixl::SUCCESS;
using hixl::TransferOp;
using hixl::TransferOpDesc;

const char *OpName(TransferOp op) {
  return op == hixl::WRITE ? "WRITE" : "READ";
}

std::uint64_t SumBytes(const std::vector<TransferOpDesc> &descs) {
  std::uint64_t total = 0U;
  for (const auto &desc : descs) {
    total += static_cast<std::uint64_t>(desc.len);
  }
  return total;
}

std::uint64_t CurrentThreadId() {
  return static_cast<std::uint64_t>(syscall(SYS_gettid));
}

void TraceSegment(const char *stage, std::uint32_t local_rank, std::uint32_t worker_id,
                  const SegmentTransferTask &task, TransferOp op, std::mutex *trace_mu,
                  std::uint64_t elapsed_us = 0U) {
  if (trace_mu == nullptr || task.descs == nullptr || local_rank != 0U) {
    return;
  }
  std::lock_guard<std::mutex> guard(*trace_mu);
  std::cout << "[TRACE] xfer " << stage << " rank=" << local_rank << " worker=" << worker_id
            << " tid=" << CurrentThreadId() << " op=" << OpName(op) << " seg=" << task.segment_id
            << " peer=" << task.endpoint << " bytes=" << SumBytes(*task.descs);
  if (elapsed_us != 0U) {
    std::cout << " elapsed=" << elapsed_us;
  }
  std::cout << std::endl;
}

void TraceLocalCopy(const char *stage, std::uint32_t local_rank, std::uint32_t worker_id,
                    const SegmentTransferTask &task, TransferOp op, std::mutex *trace_mu,
                    std::uint64_t elapsed_us = 0U) {
  if (trace_mu == nullptr || task.descs == nullptr || local_rank != 0U) {
    return;
  }
  std::lock_guard<std::mutex> guard(*trace_mu);
  std::cout << "[TRACE] copy " << stage << " rank=" << local_rank << " worker=" << worker_id
            << " tid=" << CurrentThreadId() << " op=" << OpName(op) << " seg=" << task.segment_id
            << " bytes=" << SumBytes(*task.descs);
  if (elapsed_us != 0U) {
    std::cout << " elapsed=" << elapsed_us;
  }
  std::cout << std::endl;
}

void SetWorkerContext(aclrtContext device_context) {
  if (device_context == nullptr) {
    throw std::runtime_error("missing aclrt context for transfer worker");
  }
  const auto ret = aclrtSetCurrentContext(device_context);
  if (ret != ACL_ERROR_NONE) {
    throw std::runtime_error("aclrtSetCurrentContext failed in transfer worker, ret=" + std::to_string(ret));
  }
}

void RunLocalSegmentCopy(std::uint32_t local_rank, std::uint32_t worker_id, const SegmentTransferTask &task,
                         TransferOp op, const char *(*recent_errmsg)(), std::mutex *trace_mu) {
  TraceLocalCopy("begin", local_rank, worker_id, task, op, trace_mu);
  const auto start = std::chrono::steady_clock::now();
  for (const auto &desc : *task.descs) {
    void *dst = nullptr;
    const void *src = nullptr;
    aclrtMemcpyKind kind = ACL_MEMCPY_DEVICE_TO_HOST;
    if (op == hixl::WRITE) {
      dst = reinterpret_cast<void *>(desc.remote_addr);
      src = reinterpret_cast<const void *>(desc.local_addr);
      kind = ACL_MEMCPY_DEVICE_TO_HOST;
    } else {
      dst = reinterpret_cast<void *>(desc.local_addr);
      src = reinterpret_cast<const void *>(desc.remote_addr);
      kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }
    const auto ret = aclrtMemcpy(dst, desc.len, src, desc.len, kind);
    if (ret != ACL_ERROR_NONE) {
      const char *errmsg = recent_errmsg != nullptr ? recent_errmsg() : "unknown";
      throw std::runtime_error("local aclrtMemcpy failed for segment " + std::to_string(task.segment_id) +
                               ", ret=" + std::to_string(ret) + ", errmsg: " + errmsg);
    }
  }
  const auto elapsed_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
  TraceLocalCopy("end", local_rank, worker_id, task, op, trace_mu, elapsed_us);
}

void RunOneRemoteSegment(hixl::Hixl &hixl, std::uint32_t local_rank, std::uint32_t worker_id,
                         const SegmentTransferTask &task, TransferOp op, std::int32_t timeout_ms,
                         const char *(*recent_errmsg)(), std::mutex *trace_mu) {
  if (task.descs == nullptr || task.descs->empty()) {
    throw std::runtime_error("internal error: empty remote segment transfer task");
  }
  TraceSegment("begin", local_rank, worker_id, task, op, trace_mu);
  const auto start = std::chrono::steady_clock::now();
  const auto ret = hixl.TransferSync(AscendString(task.endpoint.c_str()), op, *task.descs, timeout_ms);
  if (ret != SUCCESS) {
    const char *errmsg = recent_errmsg != nullptr ? recent_errmsg() : "unknown";
    throw std::runtime_error("TransferSync failed for segment " + std::to_string(task.segment_id) + " to " +
                             task.endpoint + ", descs=" + std::to_string(task.descs->size()) +
                             ", ret=" + std::to_string(ret) + ", errmsg: " + errmsg);
  }
  const auto elapsed_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
  TraceSegment("end", local_rank, worker_id, task, op, trace_mu, elapsed_us);
}

}  // namespace

KvTransferExecutor::KvTransferExecutor(hixl::Hixl *hixl, std::map<std::uint32_t, RankMeta> metas_by_rank,
                                       std::uint32_t self_rank, std::uint32_t num_threads,
                                       std::int32_t timeout_ms, aclrtContext device_context,
                                       const char *(*recent_errmsg)())
    : hixl_(hixl),
      metas_by_rank_(std::move(metas_by_rank)),
      self_rank_(self_rank),
      worker_count_(std::max(1U, num_threads)),
      timeout_ms_(timeout_ms),
      device_context_(device_context),
      recent_errmsg_(recent_errmsg) {
  StartWorkers();
}

KvTransferExecutor::~KvTransferExecutor() {
  StopWorkers();
}

std::vector<SegmentTransferTask> KvTransferExecutor::BuildTasks(
    const std::map<std::uint32_t, std::vector<TransferOpDesc>> &descs_by_rank) const {
  std::vector<SegmentTransferTask> tasks;
  for (const auto &item : descs_by_rank) {
    if (item.second.empty()) {
      continue;
    }
    if (item.first == self_rank_) {
      tasks.push_back(SegmentTransferTask{item.first, "", &item.second, true});
      continue;
    }
    const auto meta_it = metas_by_rank_.find(item.first);
    if (meta_it == metas_by_rank_.end()) {
      throw std::runtime_error("missing rank meta for remote transfer");
    }
    tasks.push_back(SegmentTransferTask{item.first, meta_it->second.endpoint, &item.second, false});
  }
  return tasks;
}

void KvTransferExecutor::StartWorkers() {
  workers_.reserve(worker_count_);
  try {
    for (std::uint32_t i = 0U; i < worker_count_; ++i) {
      workers_.emplace_back(&KvTransferExecutor::WorkerLoop, this, i);
    }
    WaitWorkersReady();
  } catch (...) {
    StopWorkers();
    throw;
  }
}

void KvTransferExecutor::StopWorkers() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  work_cv_.notify_all();
  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
}

void KvTransferExecutor::WaitWorkersReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  ready_cv_.wait(lock, [this]() { return ready_workers_ == worker_count_; });
  if (startup_error_) {
    std::rethrow_exception(startup_error_);
  }
}

void KvTransferExecutor::RecordErrorAndCancelPending() {
  if (!first_error_) {
    first_error_ = std::current_exception();
  }
  const auto canceled = tasks_.size() - next_task_;
  next_task_ = tasks_.size();
  remaining_tasks_ -= std::min(remaining_tasks_, canceled);
}

void KvTransferExecutor::WorkerLoop(std::uint32_t worker_id) {
  try {
    SetWorkerContext(device_context_);
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    startup_error_ = std::current_exception();
    ++ready_workers_;
    ready_cv_.notify_one();
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++ready_workers_;
    ready_cv_.notify_one();
  }
  while (true) {
    SegmentTransferTask task;
    TransferOp op = hixl::WRITE;
    bool trace_transfer = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      work_cv_.wait(lock, [this]() { return stop_ || (work_active_ && next_task_ < tasks_.size()); });
      if (stop_) {
        return;
      }
      task = tasks_.at(next_task_++);
      op = op_;
      trace_transfer = trace_transfer_;
    }
    try {
      if (task.is_self) {
        RunLocalSegmentCopy(self_rank_, worker_id, task, op, recent_errmsg_, trace_transfer ? &trace_mu_ : nullptr);
      } else {
        RunOneRemoteSegment(*hixl_, self_rank_, worker_id, task, op, timeout_ms_, recent_errmsg_,
                            trace_transfer ? &trace_mu_ : nullptr);
      }
    } catch (...) {
      std::lock_guard<std::mutex> lock(mutex_);
      RecordErrorAndCancelPending();
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (remaining_tasks_ > 0U) {
        --remaining_tasks_;
      }
      if (remaining_tasks_ == 0U && work_active_) {
        work_active_ = false;
        done_cv_.notify_one();
      }
    }
  }
}

void KvTransferExecutor::Transfer(
    TransferOp op, const std::map<std::uint32_t, std::vector<TransferOpDesc>> &descs_by_rank, bool trace_transfer) {
  auto tasks = BuildTasks(descs_by_rank);
  if (tasks.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (work_active_) {
      throw std::runtime_error("internal error: transfer executor is already active");
    }
    tasks_ = std::move(tasks);
    op_ = op;
    next_task_ = 0U;
    remaining_tasks_ = tasks_.size();
    first_error_ = nullptr;
    trace_transfer_ = trace_transfer;
    work_active_ = true;
  }
  work_cv_.notify_all();
  std::exception_ptr error;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    done_cv_.wait(lock, [this]() { return !work_active_; });
    error = first_error_;
    tasks_.clear();
  }
  if (error) {
    std::rethrow_exception(error);
  }
}

}  // namespace hixl_kv_benchmark
