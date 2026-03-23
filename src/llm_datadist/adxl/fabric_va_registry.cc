/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_va_registry.h"
#include "adxl/adxl_checker.h"
#include "adxl/adxl_types.h"
#include "common/def_types.h"
#include "common/llm_log.h"
#include "common/llm_scope_guard.h"
#include "virtual_memory_manager.h"
#include <base/err_msg.h>

namespace adxl {
namespace {

struct StagedSegment {
  uintptr_t new_va;
  aclrtDrvMemHandle pa_handle;
  VaInfo va_info;
};

void RollbackStaged(const std::vector<StagedSegment> &staged) {
  for (const auto &s : staged) {
    LLM_CHK_ACL(aclrtUnmapMem(llm::ValueToPtr(s.new_va)));
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(s.new_va);
    LLM_CHK_ACL(aclrtFreePhysical(s.pa_handle));
  }
}

}  // namespace

FabricVaRegistry &FabricVaRegistry::GetInstance() {
  static FabricVaRegistry instance;
  return instance;
}

FabricVaRegistry::~FabricVaRegistry() {
  Finalize();
}


bool FabricVaRegistry::SegmentAlreadyImported(const PeerImportedState &state, uintptr_t remote_va, size_t len) {
  for (const auto &kv : state.new_va_to_old_va) {
    const auto &info = kv.second;
    if (info.va_addr == remote_va && info.len == len) {
      return true;
    }
  }
  return false;
}

void FabricVaRegistry::ClearPeerLocked(const std::string &peer_key) {
  auto it = peers_.find(peer_key);
  if (it == peers_.end()) {
    return;
  }
  PeerImportedState &st = it->second;
  for (auto &kv : st.new_va_to_old_va) {
    LLMLOGI("Unmap mem:%lu", kv.first);
    LLM_CHK_ACL(aclrtUnmapMem(llm::ValueToPtr(kv.first)));
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(kv.first);
  }
  st.new_va_to_old_va.clear();
  for (auto &remote_pa_handle : st.remote_pa_handles) {
    LLM_CHK_ACL(aclrtFreePhysical(remote_pa_handle));
    LLMLOGI("Free imported handle:%p.", remote_pa_handle);
  }
  st.remote_pa_handles.clear();
  peers_.erase(it);
}

Status FabricVaRegistry::ImportRemoteShares(const std::string &peer_key,
                                            const std::vector<ShareHandleInfo> &remote_share_handles,
                                            int32_t device_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  PeerImportedState &state = peers_[peer_key];
  std::vector<StagedSegment> staged;
  LLM_DISMISSABLE_GUARD(fail_guard, ([&staged]() { RollbackStaged(staged); }));

  for (const auto &remote_share_handle_info : remote_share_handles) {
    if (SegmentAlreadyImported(state, remote_share_handle_info.va_addr, remote_share_handle_info.len)) {
      continue;
    }
    uintptr_t remote_va_addr = 0;
    aclrtDrvMemHandle remote_pa_handle = nullptr;
    ADXL_CHK_STATUS_RET(VirtualMemoryManager::GetInstance().ReserveMemory(remote_share_handle_info.len, remote_va_addr),
                        "Failed to reserve memory for fabric import");
    LLM_DISMISSABLE_GUARD(free_mem_guard, ([&remote_va_addr, &remote_pa_handle]() {
                            if (remote_va_addr != 0) {
                              (void)VirtualMemoryManager::GetInstance().ReleaseMemory(remote_va_addr);
                            }
                            if (remote_pa_handle != nullptr) {
                              LLM_CHK_ACL(aclrtFreePhysical(remote_pa_handle));
                            }
                          }));

    auto share_handle = remote_share_handle_info.share_handle;
    ADXL_CHK_ACL_RET(aclrtMemImportFromShareableHandleV2(&share_handle, ACL_MEM_SHARE_HANDLE_TYPE_FABRIC, 0U,
                                                         &remote_pa_handle));
    void *remote_va = llm::ValueToPtr(remote_va_addr);
    ADXL_CHK_ACL_RET(aclrtMapMem(remote_va, remote_share_handle_info.len, 0, remote_pa_handle, 0));
    staged.push_back(StagedSegment{remote_va_addr, remote_pa_handle,
                                   VaInfo{remote_share_handle_info.va_addr, remote_share_handle_info.len}});
    LLM_DISMISS_GUARD(free_mem_guard);
    LLMLOGI("Imported mem from share handle, va:%lu, new mapped va addr:%lu, len:%zu, imported handle:%p for device:%d.",
            remote_share_handle_info.va_addr, remote_va_addr, remote_share_handle_info.len, remote_pa_handle,
            device_id);
  }
  LLM_DISMISS_GUARD(fail_guard);

  for (const auto &s : staged) {
    state.new_va_to_old_va[s.new_va] = s.va_info;
    state.remote_pa_handles.push_back(s.pa_handle);
  }
  return SUCCESS;
}

std::unordered_map<uintptr_t, VaInfo> FabricVaRegistry::GetNewVaToOldVa(const std::string &peer_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = peers_.find(peer_key);
  if (it == peers_.cend()) {
    return {};
  }
  return it->second.new_va_to_old_va;
}

void FabricVaRegistry::RegisterConsumer(const std::string &peer_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  peers_[peer_key].consumer_count++;
}

void FabricVaRegistry::UnregisterConsumer(const std::string &peer_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = peers_.find(peer_key);
  if (it == peers_.end()) {
    LLMLOGW("UnregisterConsumer: unknown peer_key:%s", peer_key.c_str());
    return;
  }
  if (it->second.consumer_count > 0) {
    it->second.consumer_count--;
  }
  if (it->second.consumer_count == 0) {
    ClearPeerLocked(peer_key);
  }
}

void FabricVaRegistry::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!peers_.empty()) {
    ClearPeerLocked(peers_.begin()->first);
  }
}

}  // namespace adxl
