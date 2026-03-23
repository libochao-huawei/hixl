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
#include "acl/acl.h"
#include "adxl/adxl_checker.h"
#include "adxl/adxl_types.h"
#include "common/def_types.h"
#include "common/llm_log.h"
#include "common/llm_scope_guard.h"
#include "virtual_memory_manager.h"
#include <base/err_msg.h>

namespace adxl {
namespace {

constexpr size_t kMemAccessDescCount = 1U;

struct StagedSegment {
  uintptr_t new_va;
  aclrtDrvMemHandle pa_handle;
  VaInfo va_info;
  int32_t importer_device_id = -1;
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
  for (const auto &kv : state.imported_segments) {
    const auto &info = kv.second.va_info;
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
  for (auto &kv : st.imported_segments) {
    LLMLOGI("Unmap mem:%lu", kv.first);
    LLM_CHK_ACL(aclrtUnmapMem(llm::ValueToPtr(kv.first)));
    (void)VirtualMemoryManager::GetInstance().ReleaseMemory(kv.first);
  }
  st.imported_segments.clear();
  for (auto &remote_pa_handle : st.remote_pa_handles) {
    LLM_CHK_ACL(aclrtFreePhysical(remote_pa_handle));
    LLMLOGI("Free imported handle:%p.", remote_pa_handle);
  }
  st.remote_pa_handles.clear();
  peers_.erase(it);
}

void FabricVaRegistry::RegisterImporterContext(int32_t device_id, aclrtContext context) {
  std::lock_guard<std::mutex> lock(mutex_);
  importer_device_to_context_[device_id] = context;
}

void FabricVaRegistry::UnregisterImporterContext(int32_t device_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  (void)importer_device_to_context_.erase(device_id);
}

Status FabricVaRegistry::InvokeMemSetAccessOnImporterContext(void *vir_ptr, size_t size, int32_t target_device_id,
                                                             aclrtContext importer_context) {
  aclrtContext prev_context = nullptr;
  ADXL_CHK_ACL_RET(aclrtGetCurrentContext(&prev_context));
  LLM_MAKE_GUARD(ctx_guard, ([prev_context]() {
                   if (prev_context != nullptr) {
                   (void)aclrtSetCurrentContext(prev_context);
                   }
                   }));
  ADXL_CHK_ACL_RET(aclrtSetCurrentContext(importer_context));
  aclrtMemAccessDesc desc{};
  desc.flags = ACL_RT_MEM_ACCESS_FLAGS_READWRITE;
  desc.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  desc.location.id = static_cast<uint32_t>(target_device_id);
  ADXL_CHK_ACL_RET(aclrtMemSetAccess(vir_ptr, size, &desc, kMemAccessDescCount));
  return SUCCESS;
}

Status FabricVaRegistry::GrantImportedSegmentAccess(ImportedSegmentMeta &meta, uintptr_t new_va,
                                                    int32_t target_device_id) {
  if (meta.access_granted_devices.count(target_device_id) != 0U) {
    return SUCCESS;
  }
  const auto ctx_it = importer_device_to_context_.find(meta.importer_device_id);
  if (ctx_it == importer_device_to_context_.cend() || ctx_it->second == nullptr) {
    LLMLOGE(FAILED, "No registered importer context for logic device %d.", meta.importer_device_id);
    return FAILED;
  }
  void *vir_ptr = llm::ValueToPtr(new_va);
  ADXL_CHK_STATUS_RET(
      InvokeMemSetAccessOnImporterContext(vir_ptr, meta.va_info.len, target_device_id, ctx_it->second),
      "aclrtMemSetAccess failed for peer device.");
  (void)meta.access_granted_devices.insert(target_device_id);
  LLMLOGI("Granted fabric mem access for logic device %d on mapped va %lu, len %zu (importer device %d).",
          target_device_id, new_va, meta.va_info.len, meta.importer_device_id);
  return SUCCESS;
}

uintptr_t FabricVaRegistry::FindNewVaForRemote(const PeerImportedState &state, uintptr_t remote_va, size_t len) {
  for (const auto &kv : state.imported_segments) {
    const auto &info = kv.second.va_info;
    if (info.va_addr == remote_va && info.len == len) {
      return kv.first;
    }
  }
  return 0U;
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
      const uintptr_t new_va =
          FabricVaRegistry::FindNewVaForRemote(state, remote_share_handle_info.va_addr, remote_share_handle_info.len);
      ADXL_CHK_BOOL_RET_STATUS(new_va != 0U, FAILED, "Missing imported segment for remote va %lu.",
                               remote_share_handle_info.va_addr);
      auto seg_it = state.imported_segments.find(new_va);
      ADXL_CHK_BOOL_RET_STATUS(seg_it != state.imported_segments.end(), FAILED, "Missing segment meta for va %lu.",
                               new_va);
      ADXL_CHK_STATUS_RET(GrantImportedSegmentAccess(seg_it->second, new_va, device_id),
                          "Failed to grant fabric mem access for peer device.");
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
                                   VaInfo{remote_share_handle_info.va_addr, remote_share_handle_info.len},
                                   device_id});
    LLM_DISMISS_GUARD(free_mem_guard);
    LLMLOGI("Imported mem from share handle, va:%lu, new mapped va addr:%lu, len:%zu, imported handle:%p for device:%d.",
            remote_share_handle_info.va_addr, remote_va_addr, remote_share_handle_info.len, remote_pa_handle,
            device_id);
  }
  LLM_DISMISS_GUARD(fail_guard);

  for (const auto &s : staged) {
    ImportedSegmentMeta meta{};
    meta.va_info = s.va_info;
    meta.importer_device_id = s.importer_device_id;
    state.imported_segments[s.new_va] = std::move(meta);
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
  std::unordered_map<uintptr_t, VaInfo> out;
  out.reserve(it->second.imported_segments.size());
  for (const auto &kv : it->second.imported_segments) {
    out.emplace(kv.first, kv.second.va_info);
  }
  return out;
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
