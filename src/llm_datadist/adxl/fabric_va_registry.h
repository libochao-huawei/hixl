/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_FABRIC_VA_REGISTRY_H_
#define CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_FABRIC_VA_REGISTRY_H_

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "channel.h"

namespace adxl {

// Process-wide imported remote VA mappings for Fabric Mem, partitioned by peer_key (e.g. channel_id).
class FabricVaRegistry {
 public:
  static FabricVaRegistry &GetInstance();

  Status ImportRemoteShares(const std::string &peer_key,
                            const std::vector<ShareHandleInfo> &remote_share_handles, int32_t device_id);

  std::unordered_map<uintptr_t, VaInfo> GetNewVaToOldVa(const std::string &peer_key);

  void RegisterConsumer(const std::string &peer_key);
  void UnregisterConsumer(const std::string &peer_key);

  // Clears all peer buckets (unmap/free). Call after all fabric Channel objects have Finalized
  // (UnregisterConsumer), e.g. from engine shutdown; used as a safety net for leftover state.
  void Finalize();

  FabricVaRegistry(const FabricVaRegistry &) = delete;
  FabricVaRegistry &operator=(const FabricVaRegistry &) = delete;

 private:
  FabricVaRegistry() = default;
  ~FabricVaRegistry();

  struct PeerImportedState {
    int consumer_count = 0;
    std::unordered_map<uintptr_t, VaInfo> new_va_to_old_va;
    std::vector<aclrtDrvMemHandle> remote_pa_handles;
  };

  static bool SegmentAlreadyImported(const PeerImportedState &state, uintptr_t remote_va, size_t len);
  void ClearPeerLocked(const std::string &peer_key);

  std::mutex mutex_;
  std::unordered_map<std::string, PeerImportedState> peers_;
};

}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_RUNTIME_LLM_DATADIST_V2_FABRIC_VA_REGISTRY_H_
