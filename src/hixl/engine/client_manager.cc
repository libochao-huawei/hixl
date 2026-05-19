/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>

#include "hixl_engine.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "client_manager.h"

namespace hixl {
namespace {
constexpr int64_t kHeartbeatIntervalMs = 10000;  // 10 seconds
}

Status ClientManager::Initialize(bool auto_connect) {
  auto_connect_ = auto_connect;
  stop_signal_.store(false);
  // 开启Auto Connect才会进行心跳检测
  if (!auto_connect_) {
    return SUCCESS;
  }
  return StartHeartbeat();
}

Status ClientManager::StartHeartbeat() {
  aclrtContext aclrt_context = nullptr;
  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&aclrt_context), "Get aclrt context for heartbeat thread failed.");
  heartbeat_sender_ = std::thread([this, aclrt_context]() -> Status {
    if (aclrt_context != nullptr) {
      HIXL_CHK_ACL_RET(aclrtSetCurrentContext(aclrt_context), "Set aclrt context in heartbeat thread failed.");
    }
    std::unique_lock<std::mutex> lock(cv_mutex_);
    while (!stop_signal_.load()) {
      CheckAlive();
      cv_.wait_for(lock, std::chrono::milliseconds(kHeartbeatIntervalMs),
                   [this] { return stop_signal_.load(); });
    }
    return SUCCESS;
  });
  return SUCCESS;
}

Status ClientManager::CreateClient(const ClientConfig &config, ClientPtr &client_ptr) {
  std::string ip;
  int32_t port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(config.remote_engine, ip, port), "Failed to parse ip, remote_engine:%s",
                      config.remote_engine.c_str());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finalized_) {
      HIXL_LOGE(FAILED, "ClientManager already finalized, cannot create new client");
      return FAILED;
    }
  }
  client_ptr = MakeShared<HixlClient>(ip, static_cast<uint32_t>(port), config);
  ClientConfig client_config = config;
  client_ptr = MakeShared<HixlClient>(ip, static_cast<uint32_t>(port), client_config);
  HIXL_CHECK_NOTNULL(client_ptr, "Failed to create HixlClient, ip:%s, port:%u", ip.c_str(), port);
  HIXL_CHK_STATUS_RET(client_ptr->Initialize(config.endpoint_list), "Failed to initialize HixlClient, ip:%s, port:%u",
                      ip.c_str(), port);
  return SUCCESS;
}

Status ClientManager::GetOrCreateClient(const ClientConfig &config, const std::vector<MemInfo> &mem_info_list,
                                        int32_t timeout_in_millis, ClientPtr &client_ptr) {
  auto client_mutex = GetClientMutex(config.remote_engine);
  std::lock_guard<std::mutex> client_lock(*client_mutex);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = clients_.find(config.remote_engine);
    if (it != clients_.cend()) {
      client_ptr = it->second;
      return ALREADY_CONNECTED;
    }
  }

  ClientPtr new_client = nullptr;
  HIXL_CHK_STATUS_RET(CreateClient(config, new_client),
                      "Failed to create HixlClient, remote_engine:%s", config.remote_engine.c_str());
  HIXL_CHECK_NOTNULL(new_client, "Created client is null, remote_engine:%s", config.remote_engine.c_str());
  Status ret = new_client->SetLocalMemInfo(mem_info_list);
  if (ret != SUCCESS) {
    (void)new_client->Finalize();
    HIXL_LOGE(ret, "Failed to set local memory info, remote_engine:%s", config.remote_engine.c_str());
    return ret;
  }
  ret = new_client->Connect(timeout_in_millis);
  if (ret != SUCCESS) {
    (void)new_client->Finalize();
    HIXL_LOGE(ret, "Failed to connect client, remote_engine:%s, timeout:%d ms",
              config.remote_engine.c_str(), timeout_in_millis);
    return ret;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  clients_.emplace(config.remote_engine, new_client);
  client_ptr = new_client;
  return SUCCESS;
}

ClientPtr ClientManager::GetClient(const std::string &remote_engine) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = clients_.find(remote_engine);
  if (it != clients_.cend()) {
    return it->second;
  }
  return nullptr;
}

Status ClientManager::DestroyClient(const std::string &remote_engine) {
  auto ret = NOT_CONNECTED;
  ClientPtr client = nullptr;
  auto client_mutex = GetClientMutex(remote_engine);
  std::lock_guard<std::mutex> client_lock(*client_mutex);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = clients_.find(remote_engine);
    if (it != clients_.end()) {
      client = it->second;
      clients_.erase(it);
      HIXL_LOGI("Erase client end, remote_engine=%s", remote_engine.c_str());
    }
  }

  if (client != nullptr) {
    ret = client->Finalize();
    HIXL_LOGI("Destroy client end, remote_engine=%s", remote_engine.c_str());
  }
  client_mutex.reset();
  DestroyClientMutex(remote_engine);
  return ret;
}

std::shared_ptr<std::mutex> ClientManager::GetClientMutex(const std::string &remote_engine) {
  std::lock_guard<std::mutex> lock(client_mutexes_mutex_);
  auto it = client_mutexes_.find(remote_engine);
  if (it != client_mutexes_.end()) {
    return it->second;
  }

  auto client_mutex = std::make_shared<std::mutex>();
  client_mutexes_.emplace(remote_engine, client_mutex);
  return client_mutex;
}

void ClientManager::DestroyClientMutex(const std::string &remote_engine) {
  std::lock_guard<std::mutex> lock(client_mutexes_mutex_);
  auto it = client_mutexes_.find(remote_engine);
  if (it != client_mutexes_.end()) {
    client_mutexes_.erase(it);
  }
}

bool ClientManager::IsEmpty() {
  std::lock_guard<std::mutex> lock(mutex_);
  return clients_.empty();
}

void ClientManager::CheckAlive() {
  std::map<std::string, ClientPtr> clients_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_copy = clients_;
  }

  std::vector<std::string> dead_clients;
  for (const auto &[name, client] : clients_copy) {
    bool need_retry = true;
    Status ret = client->SendHeartbeat(need_retry);
    if (ret == SUCCESS && !need_retry) {
      HIXL_LOGW("Heartbeat broken pipe for remote_engine:%s", name.c_str());
      dead_clients.push_back(name);
    } else if (ret != SUCCESS) {
      HIXL_LOGW("Heartbeat send failed for remote_engine:%s, ret:%u", name.c_str(), static_cast<uint32_t>(ret));
    }
  }

  if (auto_connect_) {
    for (const auto &name : dead_clients) {
      HIXL_LOGI("Heartbeat broken pipe, destroying client for auto reconnect: %s", name.c_str());
      (void)DestroyClient(name);
    }
  }
}

Status ClientManager::Finalize() {
  stop_signal_.store(true);
  cv_.notify_all();
  if (heartbeat_sender_.joinable()) {
    heartbeat_sender_.join();
  }
  std::map<std::string, ClientPtr> clients;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    finalized_ = true;
    clients = std::move(clients_);
  }

  for (auto &it : clients) {
    if (it.second->Finalize() != SUCCESS) {
      HIXL_LOGE(FAILED, "Failed to finalize client, remote_engine:%s", it.first.c_str());
    }
  }
  clients_.clear();
  {
    std::lock_guard<std::mutex> client_mutexes_lock(client_mutexes_mutex_);
    client_mutexes_.clear();
  }
  return SUCCESS;
}
}  // namespace hixl