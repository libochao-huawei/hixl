/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <list>
#include <mutex>
#include "hixl/hixl.h"
#include "common/hixl_checker.h"
#include "common/hixl_utils.h"
#include "adxl_engine.h"
#include "base/err_msg.h"
#include "engine.h"
#include "engine_factory.h"

namespace hixl {
namespace {
Status CheckTransferOpDescs(const std::vector<TransferOpDesc> &op_descs) {
  for (const auto &desc : op_descs) {
    auto local_addr = reinterpret_cast<void *>(desc.local_addr);
    auto remote_addr = reinterpret_cast<void *>(desc.remote_addr);
    HIXL_CHK_BOOL_RET_STATUS(local_addr != nullptr,
                             PARAM_INVALID, "local addr of desc can not be null.");
    HIXL_CHK_BOOL_RET_STATUS(remote_addr != nullptr,
                             PARAM_INVALID, "remote addr of desc can not be null.");
  }
  return SUCCESS;
}

struct ConnectPoolExecutorTask {
  bool is_connect;
  AscendString remote_engine;
  std::function<void()> task;
};
}

class ConnectPoolExecutor {
 public:
  ConnectPoolExecutor();
  ~ConnectPoolExecutor();

  Status Initialize(const std::map<AscendString, AscendString> &options);

  void Shutdown();

  Status Submit(const std::function<void()>& task, const AscendString &remote_engine, const bool is_connect);

  void SetStatus(const AscendString &remote_engine, const AsyncConnectStatus status);

  Status GetStatus(const AscendString &remote_engine, AsyncConnectStatus& status);

  Status GetStatus(std::map<AscendString, AsyncConnectStatus>& status_map);

 private:
  bool IsInitialized() const;

  void ParseGlobalResourceConfig(const std::map<AscendString, AscendString> &options);

  void WorkerHandler(const int i);

  int thread_num_{0};
  int task_queue_capacity_{0};
  std::atomic<bool> is_initialized_{false};
  std::atomic<bool> is_shutdown_{false};
  std::vector<std::thread> workers_;

  std::mutex task_queue_mutex_;
  std::condition_variable task_queue_cv_;
  std::list<ConnectPoolExecutorTask> task_list_;
  std::unordered_map<AscendString, std::list<ConnectPoolExecutorTask>::iterator> task_map_;

  std::mutex task_result_mutex_;
  std::map<AscendString, AsyncConnectStatus> task_result_;

  aclrtContext ctx_ = nullptr;
};

ConnectPoolExecutor::ConnectPoolExecutor() {}

ConnectPoolExecutor::~ConnectPoolExecutor() {
  if (is_shutdown_.load(std::memory_order::memory_order_relaxed) == false) {
    Shutdown();
  }
}

Status ConnectPoolExecutor::Initialize(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("ConnectPoolExecutor initialize start");
  HIXL_CHK_BOOL_RET_STATUS(!IsInitialized(), SUCCESS, "ConnectPoolExecutor is already initialized");
  ParseGlobalResourceConfig(options);
  HIXL_CHK_BOOL_RET_STATUS(thread_num_ >= 1 && thread_num_ <= 64, PARAM_INVALID, "thread_num:%d must in [1, 64]",
                           thread_num_);
  HIXL_CHK_BOOL_RET_STATUS(task_queue_capacity_ >= 1 && task_queue_capacity_ <= 65535, PARAM_INVALID,
                           "task_queue_capacity:%d must in [1, 65535]", thread_num_);

  (void)aclrtGetCurrentContext(&ctx_);
  for (int i = 0; i < thread_num_; ++i) {
    workers_.emplace_back([this, i]() { WorkerHandler(i); });
  }
  is_initialized_.store(true, std::memory_order::memory_order_relaxed);
  is_shutdown_.store(false, std::memory_order::memory_order_relaxed);
  HIXL_LOGI("ConnectPoolExecutor initialize success");
  return SUCCESS;
}

void ConnectPoolExecutor::Shutdown() {
  HIXL_LOGI("ConnectPoolExecutor shutdown start");
  if (is_shutdown_.load(std::memory_order::memory_order_relaxed) == true) {
    HIXL_LOGW("ConnectPoolExecutor is already shutdown");
    return;
  }

  is_shutdown_.store(true, std::memory_order::memory_order_relaxed);
  task_queue_cv_.notify_all();
  for (auto &worker : workers_) {
    worker.join();
  }
  workers_.clear();
  is_initialized_.store(false, std::memory_order::memory_order_relaxed);
  HIXL_LOGI("ConnectPoolExecutor shutdown success");
}

Status ConnectPoolExecutor::Submit(const std::function<void()> &task, const AscendString &remote_engine,
                                   const bool is_connect) {
  std::unique_lock<std::mutex> lock(task_queue_mutex_);
  HIXL_CHK_BOOL_RET_STATUS(task_list_.size() < static_cast<std::size_t>(task_queue_capacity_), RESOURCE_EXHAUSTED,
                           "task_queue is full, task_queue_capacity:%d", task_queue_capacity_);
  auto it = task_map_.find(remote_engine);
  if (it != task_map_.end()) {
    if (it->second->is_connect == is_connect) {
      // 任务队列中已有相同任务，则忽略新任务
      HIXL_LOGI("ignore task %s to %s", is_connect ? "connect" : "disconnect", remote_engine.GetString());
      return SUCCESS;
    }

    // 任务队列中已有不同任务，则删除已有任务
    HIXL_LOGI("remove task %s to %s", it->second->is_connect ? "connect" : "disconnect", remote_engine.GetString());
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

Status ConnectPoolExecutor::GetStatus(std::map<AscendString, AsyncConnectStatus> &status_map) {
  status_map.clear();
  std::unique_lock<std::mutex> lock(task_result_mutex_);
  std::copy(task_result_.begin(), task_result_.end(), std::inserter(status_map, status_map.begin()));
  return SUCCESS;
}

bool ConnectPoolExecutor::IsInitialized() const {
  return is_initialized_.load(std::memory_order::memory_order_relaxed);
}

void ConnectPoolExecutor::ParseGlobalResourceConfig(const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("ParseGlobalResourceConfig start");
  thread_num_ = hixl::OPTION_CONNECT_POOL_THREAD_NUM;
  task_queue_capacity_ = hixl::OPTION_CONNECT_POOL_TASK_QUEUE_CAPACITY;

  const auto &it = options.find(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  if (it == options.end()) {
    HIXL_LOGW("Failed to find %s, use default", hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
    return;
  }

  std::string config_str = it->second.GetString();
  if (config_str.empty()) {
    HIXL_LOGW("Failed to parse empty %s, use default", hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
    return;
  }

  try {
    auto config = nlohmann::json::parse(config_str);
    std::string thread_num = config["connect_pool.thread_num"];
    std::string task_queue_capacity = config["connect_pool.task_queue_capacity"];
    thread_num_ = std::stoi(thread_num);
    task_queue_capacity_ = std::stoi(task_queue_capacity);
  } catch (const std::exception &e) {
    HIXL_LOGW("Failed to parse %s error %s, use default", hixl::OPTION_GLOBAL_RESOURCE_CONFIG, e.what());
    return;
  }
  HIXL_LOGI("ParseGlobalResourceConfig success, thread_num:%d task_queue_capacity:%d", thread_num_,
            task_queue_capacity_);
}

void ConnectPoolExecutor::WorkerHandler(const int i) {
  HIXL_LOGI("ConnectPoolExecutor worker %d start", i);
  HIXL_CHK_ACL(aclrtSetCurrentContext(ctx_));
  while (true) {
    ConnectPoolExecutorTask executor_task{false, "", nullptr};
    {
      std::unique_lock<std::mutex> lock(task_queue_mutex_);
      task_queue_cv_.wait(
          lock, [this]() { return is_shutdown_.load(std::memory_order::memory_order_relaxed) || !task_list_.empty(); });

      if (is_shutdown_.load(std::memory_order::memory_order_relaxed)) {
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
      HIXL_LOGI("worker %d exec task %s to %s start", i, executor_task.is_connect ? "connect" : "disconnect",
                executor_task.remote_engine.GetString());
      executor_task.task();
      HIXL_LOGI("worker %d exec task %s to %s success", i, executor_task.is_connect ? "connect" : "disconnect",
                executor_task.remote_engine.GetString());
    }
  }
  HIXL_LOGI("ConnectPoolExecutor worker %d exit", i);
}

class Hixl::HixlImpl {
 public:
  explicit HixlImpl(const AscendString &local_engine) : local_engine_(local_engine.GetString()) {}
  ~HixlImpl() = default;

  Status Initialize(const std::map<AscendString, AscendString> &options);

  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);

  Status DeregisterMem(MemHandle mem_handle);

  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  Status ConnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  Status DisconnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  Status GetAsyncConnectStatus(const AscendString &remote_engine, AsyncConnectStatus& status);

  Status GetAsyncConnectStatus(std::map<AscendString, AsyncConnectStatus>& status_map);

  Status TransferSync(const AscendString &remote_engine,
                      TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout_in_millis = 1000);

  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req);

  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, uint32_t timeout_in_millis);

  Status GetNotifies(std::vector<NotifyDesc> &notifies);

 private:
  std::mutex mutex_;
  std::string local_engine_;
  std::unique_ptr<Engine> engine_ = nullptr;
};

Status Hixl::HixlImpl::Initialize(const std::map<AscendString, AscendString> &options) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (engine_ != nullptr) {
    HIXL_CHK_BOOL_RET_SPECIAL_STATUS(engine_->IsInitialized(), SUCCESS, "Already initialized");
  }
  engine_ = hixl::EngineFactory::CreateEngine(local_engine_, options);
  HIXL_CHECK_NOTNULL(engine_, "[HixlEngine] Created engine is null, please check your parameters! local_engine:%s", 
                     local_engine_.c_str());
  HIXL_CHK_STATUS_RET(engine_->Initialize(options), "Failed to initialize Hixl.");
  return SUCCESS;
}

void Hixl::HixlImpl::Finalize() {
  if (engine_ == nullptr) {
    HIXL_LOGE(FAILED, "engine is nullptr, check engine init");
    return;
  }
  engine_->Finalize();
  engine_.reset();
}

Status Hixl::HixlImpl::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_BOOL_RET_STATUS(reinterpret_cast<void *>(mem.addr) != nullptr,
                           PARAM_INVALID, "mem.addr can not be null");
  HIXL_CHK_STATUS_RET(engine_->RegisterMem(mem, type, mem_handle),
                      "Failed to register mem");
  return SUCCESS;
}

Status Hixl::HixlImpl::DeregisterMem(MemHandle mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_BOOL_RET_STATUS(mem_handle != nullptr, PARAM_INVALID, "mem_handle can not be null");
  HIXL_CHK_STATUS_RET(engine_->DeregisterMem(mem_handle), "Failed to deregister mem");
  return SUCCESS;
}

Status Hixl::HixlImpl::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->Connect(remote_engine, timeout_in_millis), "Failed to connect");
  return SUCCESS;
}

Status Hixl::HixlImpl::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->Disconnect(remote_engine, timeout_in_millis), "Failed to disconnect");
  return SUCCESS;
}

Status Hixl::HixlImpl::ConnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  auto task = [this, remote_engine, timeout_in_millis] () {
    HIXL_LOGI("connect async to %s %d start", remote_engine.GetString(), timeout_in_millis);
    Status ret = engine_->Connect(remote_engine, timeout_in_millis);
    const auto status = (ret == SUCCESS || ret == ALREADY_CONNECTED) ? AsyncConnectStatus::CONNECTED : AsyncConnectStatus::CONNECT_FAILED;
    HIXL_LOGI("connect async to %s %d ret %d status %d", remote_engine.GetString(), timeout_in_millis, ret, status);
  };
  return SUCCESS;
}

Status Hixl::HixlImpl::DisconnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  auto task = [this, remote_engine, timeout_in_millis] () {
    HIXL_LOGI("disconnect async to %s %d start", remote_engine.GetString(), timeout_in_millis);
    Status ret = engine_->Disconnect(remote_engine, timeout_in_millis);
    HIXL_LOGI("disconnect async to %s %d ret %d", remote_engine.GetString(), timeout_in_millis, ret);
  };
  return SUCCESS;
}

Status Hixl::HixlImpl::GetAsyncConnectStatus(const AscendString &remote_engine, AsyncConnectStatus& status) {
  (void)remote_engine;
  (void)status;
  return SUCCESS;
}

Status Hixl::HixlImpl::GetAsyncConnectStatus(std::map<AscendString, AsyncConnectStatus>& status_map) {
  (void)status_map;
  return SUCCESS;
}

Status Hixl::HixlImpl::TransferSync(const AscendString &remote_engine,
                                    TransferOp operation,
                                    const std::vector<TransferOpDesc> &op_descs,
                                    int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(CheckTransferOpDescs(op_descs), "Failed to check transfer op descs");
  HIXL_CHK_STATUS_RET(engine_->TransferSync(remote_engine, operation,
                                            op_descs, timeout_in_millis),
                                            "Failed to transfer sync.");
  return SUCCESS;
}

Status Hixl::HixlImpl::TransferAsync(const AscendString &remote_engine,
                                     TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs,
                                     const TransferArgs &optional_args,
                                     TransferReq &req) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized.");
  HIXL_CHK_STATUS_RET(CheckTransferOpDescs(op_descs), "Failed to check transfer op descs.");
  HIXL_CHK_STATUS_RET(engine_->TransferAsync(remote_engine, operation, 
                                             op_descs, optional_args, req),
                                             "Failed to transfer request async.");
  return SUCCESS;
}

Status Hixl::HixlImpl::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  TransferStatus transfer_status = TransferStatus::WAITING;
  auto ret = engine_->GetTransferStatus(req, transfer_status);
  if (ret != SUCCESS) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(ret, "Failed to get transfer status.");
    return ret;
  }          
  status = transfer_status;
  return SUCCESS;
}

Status Hixl::HixlImpl::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, uint32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->SendNotify(remote_engine, notify, timeout_in_millis), 
                      "Failed to send notify to remote engine:%s", remote_engine.GetString());
  return SUCCESS;
}

Status Hixl::HixlImpl::GetNotifies(std::vector<NotifyDesc> &notifies) {
  HIXL_CHK_BOOL_RET_STATUS(engine_ != nullptr, FAILED, "engine is nullptr, check engine init");
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->GetNotifies(notifies), 
                      "Failed to get notifies");
  return SUCCESS;
}

Hixl::Hixl() {}

Hixl::~Hixl() {
  Finalize();
}

Status Hixl::Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("Hixl initialize start");
  auto impl = llm::MakeUnique<HixlImpl>(local_engine);
  HIXL_CHK_BOOL_RET_STATUS(impl != nullptr, FAILED, "impl is nullptr, check Hixl construct");
  HIXL_CHK_STATUS_RET(impl->Initialize(options), "Failed to initialize Hixl");
  impl_ = std::move(impl);
  HIXL_LOGI("Hixl initialized successfully");
  return SUCCESS;
}

void Hixl::Finalize() {
  HIXL_LOGI("Hixl finalize start");
  if (impl_ != nullptr) {
    impl_->Finalize();
    impl_.reset();
  }
  HIXL_LOGI("Hixl finalized successfully");
}

Status Hixl::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_LOGI("RegisterMem start, type:%d, addr:%p, size:%zu",
         static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->RegisterMem(mem, type, mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to register mem, "
                           "type:%d, addr:%p, size:%lu",
                           static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_LOGI("RegisterMem success, type:%d, addr:%p, size:%zu, handle:%p",
            static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len, mem_handle);
  return SUCCESS;
}

Status Hixl::DeregisterMem(MemHandle mem_handle) {
  HIXL_LOGI("DeregisterMem start, mem_handle:%p", mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->DeregisterMem(mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to deregister mem, mem_handle:%p",
                           mem_handle);
  HIXL_LOGI("DeregisterMem success, mem_handle:%p", mem_handle);
  return SUCCESS;
}

Status Hixl::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("Connect start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->Connect(remote_engine, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to connect, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("Connect success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("Disconnect start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->Disconnect(remote_engine, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to disconnect, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("Disconnect success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::ConnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("ConnectAsync start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->ConnectAsync(remote_engine, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to connect async, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("ConnectAsync success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::DisconnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("DisconnectAsync start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->DisconnectAsync(remote_engine, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to disconnect async, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("DisconnectAsync success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::GetAsyncConnectStatus(const AscendString &remote_engine, AsyncConnectStatus& status) {
  HIXL_LOGI("GetConnectAsyncStatus start, remote engine:%s", remote_engine.GetString());
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->GetAsyncConnectStatus(remote_engine, status);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to get async connect status, remote engine:%s", remote_engine.GetString());
  HIXL_LOGI("GetConnectAsyncStatus success, remote engine:%s status:%d", remote_engine.GetString(), status);
  return SUCCESS;
}

Status Hixl::GetAsyncConnectStatus(std::map<AscendString, AsyncConnectStatus>& status_map) {
  HIXL_LOGI("GetConnectAsyncStatus start");
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->GetAsyncConnectStatus(status_map);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to get async connect status");
  HIXL_LOGI("GetConnectAsyncStatus success, status_map.size:%llu", status_map.size());
  return SUCCESS;
}

Status Hixl::TransferSync(const AscendString &remote_engine,
                                TransferOp operation,
                                const std::vector<TransferOpDesc> &op_descs,
                                int32_t timeout_in_millis) {
  HIXL_LOGI("TransferSync start, remote_engine:%s, operation:%s, op_descs size:%zu, timeout:%d ms",
         remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->TransferSync(remote_engine, operation, op_descs, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to TransferSync, remote_engine:%s, operation:%s, op_descs size:%zu, timeout:%d ms",
                           remote_engine.GetString(), TransferOpToString(operation).c_str(),
                           op_descs.size(), timeout_in_millis);
  HIXL_LOGI("TransferSync success, remote_engine:%s, operation:%s, op_descs size:%zu, timeout:%d ms",
          remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::TransferAsync(const AscendString &remote_engine,
                           TransferOp operation,
                           const std::vector<TransferOpDesc> &op_descs,
                           const TransferArgs &optional_args,
                           TransferReq &req) {
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "HixlImpl is nullptr, check Hixl init.");
  const auto ret = impl_->TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to transfer async, remote_engine:%s, operation:%s, op_descs size:%zu.",
                           remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size());
  HIXL_LOGI("Transfer async success, remote_engine:%s, operation:%s, op_descs size:%zu.",
            remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size());
  return SUCCESS;
}

Status Hixl::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "Impl is nullptr, check Hixl init.");
  HIXL_CHK_BOOL_RET_STATUS(req != nullptr, FAILED, "Req is nullptr, check req.");
  const auto ret = impl_->GetTransferStatus(req, status);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to get transfer status, req:%llu.", 
                           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(req)));
  return SUCCESS;
}

Status Hixl::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  HIXL_LOGI("SendNotify start, remote engine:%s, notify name:%s", remote_engine.GetString(), notify.name.GetString());
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  constexpr uint32_t kMaxNotifyLength = 1024U;
  HIXL_CHK_BOOL_RET_STATUS(notify.name.GetLength() <= kMaxNotifyLength, PARAM_INVALID,
                           "notify.name length exceed max limit: %u, current: %zu", kMaxNotifyLength, notify.name.GetLength());
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(notify.notify_msg.GetLength() <= kMaxNotifyLength, PARAM_INVALID,
                           "notify.notify_msg length exceed max limit: %u, current: %zu", kMaxNotifyLength, notify.notify_msg.GetLength());
  const auto ret = impl_->SendNotify(remote_engine, notify, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to send notify, remote engine:%s, notify name:%s",
                           remote_engine.GetString(), notify.name.GetString());
  HIXL_LOGI("SendNotify success, remote engine:%s, notify name:%s", remote_engine.GetString(), notify.name.GetString());
  return SUCCESS;
}

Status Hixl::GetNotifies(std::vector<NotifyDesc> &notifies) {
  HIXL_LOGI("GetNotifies start");
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->GetNotifies(notifies);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to get notifies");
  HIXL_LOGI("GetNotifies success, got %zu notifies", notifies.size());
  return SUCCESS;
}
}  // namespace hixl