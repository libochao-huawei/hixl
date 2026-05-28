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
  //开启Auto Connect才会进行心跳检测
  if (!auto_connect_) {
    return SUCCESS;
  }
  aclrtContext aclrt_context = nullptr;
  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(&aclrt_context), "Get aclrt context for heartbeat thread failed.");
  heartbeat_sender_ = std::thread([this, aclrt_context]() -> Status {
    if (aclrt_context != nullptr) {
      HIXL_CHK_ACL_RET(aclrtSetCurrentContext(aclrt_context), "Set aclrt context in heartbeat thread failed.");
    }
    std::unique_lock<std::mutex> lock(cv_mutex_);
    while (!stop_signal_.load()) {
      SendHeartbeats();
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
  ClientConfig client_config = config;
  client_config.auto_connect = auto_connect_;
  client_ptr = MakeShared<HixlClient>(ip, static_cast<uint32_t>(port), client_config);
  HIXL_CHECK_NOTNULL(client_ptr, "Failed to create HixlClient, ip:%s, port:%u", ip.c_str(), port);
  HIXL_CHK_STATUS_RET(client_ptr->Initialize(client_config.endpoint_list),
                      "Failed to initialize HixlClient, ip:%s, port:%u",
                      ip.c_str(), port);
  std::lock_guard<std::mutex> lock(mutex_);
  clients_.emplace(config.remote_engine, client_ptr);
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = clients_.find(remote_engine);
    if (it != clients_.end()) {
      auto client = it->second;
      ret = client->Finalize();
      clients_.erase(it);
      HIXL_LOGI("Destroy client end, remote_engine=%s", remote_engine.c_str());
    }
  }
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
  if (it != client_mutexes_.end() && it->second.use_count() == 1) {
    client_mutexes_.erase(it);
  }
}

bool ClientManager::IsEmpty() {
  return clients_.empty();
}

void ClientManager::SendHeartbeats() {
  if (!auto_connect_) {
    return;
  }
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
      client->StopHeartbeat();
      HIXL_LOGW("Heartbeat broken pipe for remote_engine:%s, heartbeat stopped", name.c_str());
      dead_clients.push_back(name);
    } else if (ret != SUCCESS) {
      HIXL_LOGW("Heartbeat send failed for remote_engine:%s, ret:%u", name.c_str(), static_cast<uint32_t>(ret));
    }
  }

  if (auto_connect_) {
    for (const auto &name : dead_clients) {
      HIXL_LOGI("Heartbeat broken pipe, destroying client for auto reconnect: %s", name.c_str());
      (void)DestroyClient(name);
      DestroyClientMutex(name);
    }
  }
}

Status ClientManager::Finalize() {
  stop_signal_.store(true);
  cv_.notify_all();
  if (heartbeat_sender_.joinable()) {
    heartbeat_sender_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &it : clients_) {
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
