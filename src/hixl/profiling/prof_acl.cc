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
#include <cstring>
#include <map>
#include <mutex>
#include "acl/acl.h"
#include "common/hixl_log.h"
#include "prof_proxy.h"

extern "C" {
__attribute__((weak)) void aclprofDestroyStamp(void *stamp);
__attribute__((weak)) int32_t aclprofSetStampTraceMessage(void *stamp, const char *message, uint32_t length);
__attribute__((weak)) int32_t aclprofRangeStart(void *stamp, uint32_t *range_id);
__attribute__((weak)) int32_t aclprofRangeStop(uint32_t range_id);
}

namespace {
std::mutex g_range_mutex;
std::map<uint32_t, void *> g_acl_range_stamps;

void DestroyAclStamp(void *stamp) {
  if (stamp != nullptr) {
    aclprofDestroyStamp(stamp);
  }
}

void StopAclRange(uint32_t range_id, void *stamp) {
  if (stamp == nullptr) {
    return;
  }
  if (aclprofRangeStop != nullptr) {
    const int32_t stop_ret = aclprofRangeStop(range_id);
    if (stop_ret != ACL_SUCCESS) {
      HIXL_LOGE(stop_ret, "[Hixl Profiling] Call api:aclprofRangeStop failed, ret:%d, range_id:%u", stop_ret, range_id);
    }
  }
  DestroyAclStamp(stamp);
}

bool CheckAclProfApis() {
  static const bool acl_prof_apis_available = []() {
    const bool available = (aclprofCreateStamp != nullptr) && (aclprofDestroyStamp != nullptr) &&
                           (aclprofSetStampTraceMessage != nullptr) && (aclprofRangeStart != nullptr);
    if (!available) {
      HIXL_LOGE(ACL_ERROR_PROFILING_FAILURE, "[Hixl Profiling] aclprof APIs are unavailable");
    }
    return available;
  }();
  return acl_prof_apis_available;
}

void *StartAclStampAfterCreate(void *stamp, const char *name, uint32_t &range_id) {
  const int32_t message_ret = aclprofSetStampTraceMessage(stamp, name, static_cast<uint32_t>(strlen(name)));
  if (message_ret != ACL_SUCCESS) {
    HIXL_LOGE(message_ret, "[Hixl Profiling] Call api:aclprofSetStampTraceMessage failed, ret:%d, name:%s", message_ret,
              name);
    DestroyAclStamp(stamp);
    return nullptr;
  }
  const int32_t start_ret = aclprofRangeStart(stamp, &range_id);
  if (start_ret != ACL_SUCCESS) {
    HIXL_LOGE(start_ret, "[Hixl Profiling] Call api:aclprofRangeStart failed, ret:%d, name:%s", start_ret, name);
    DestroyAclStamp(stamp);
    return nullptr;
  }
  return stamp;
}

void *CreateAclProfStamp(const char *name, uint32_t &range_id) {
  if (!CheckAclProfApis()) {
    return nullptr;
  }
  void *stamp = aclprofCreateStamp();
  if (stamp == nullptr) {
    return nullptr;
  }
  return StartAclStampAfterCreate(stamp, name, range_id);
}

uint64_t StartAclProfRange(const char *name) {
  uint32_t range_id = 0U;
  void *stamp = CreateAclProfStamp(name, range_id);
  if (stamp == nullptr) {
    return hixl::kInvalidProfRange;
  }
  std::lock_guard<std::mutex> lock(g_range_mutex);
  const auto result = g_acl_range_stamps.emplace(range_id, stamp);
  if (!result.second) {
    StopAclRange(range_id, stamp);
    return hixl::kInvalidProfRange;
  }
  return range_id;
}

void StopAclProfRange(const uint64_t range_id) {
  void *stamp = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_range_mutex);
    const auto iter = g_acl_range_stamps.find(static_cast<uint32_t>(range_id));
    if (iter == g_acl_range_stamps.end()) {
      return;
    }
    stamp = iter->second;
    g_acl_range_stamps.erase(iter);
  }
  StopAclRange(static_cast<uint32_t>(range_id), stamp);
}

void DestroyAclProfRange(const uint64_t range_id) {
  void *stamp = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_range_mutex);
    const auto iter = g_acl_range_stamps.find(static_cast<uint32_t>(range_id));
    if (iter == g_acl_range_stamps.end()) {
      return;
    }
    stamp = iter->second;
    g_acl_range_stamps.erase(iter);
  }
  DestroyAclStamp(stamp);
}

}  // namespace

namespace hixl {
uint64_t AclStartRange(const HixlProfType prof_type) {
  return StartAclProfRange(GetProfName(prof_type));
}

void AclStopRange(const uint64_t range_id) {
  StopAclProfRange(range_id);
}

void AclDestroyRange(const uint64_t range_id) {
  DestroyAclProfRange(range_id);
}
}  // namespace hixl
