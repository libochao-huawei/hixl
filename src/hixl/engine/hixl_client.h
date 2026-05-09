/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "common/segment.h"
#include "common/ctrl_msg.h"
#include "engine/client_handler.h"

namespace hixl {

struct ClientConfig {
  std::vector<EndpointConfig> endpoint_list;
  std::string remote_engine;
  uint8_t rdma_tc;
  uint8_t rdma_sl;
};

struct MatchKey {
  std::string dst_eid;
  std::string plane;
  std::string placement;

  std::string ToString() const {
    std::ostringstream oss;
    oss << "MatchKey{";
    oss << "dst_eid: " << dst_eid << ", ";
    oss << "plane: " << plane << ", ";
    oss << "placement: " << placement;
    oss << "}";
    return oss.str();
  }

  bool operator<(const MatchKey &other) const {
    if (dst_eid != other.dst_eid) {
      return dst_eid < other.dst_eid;
    } else if (plane != other.plane) {
      return plane < other.plane;
    } else {
      return placement < other.placement;
    }
  }

  bool Matches(const MatchKey &query) const {
    if (!dst_eid.empty() && !query.dst_eid.empty() && (dst_eid != query.dst_eid)) {
      return false;
    }
    if (plane != query.plane) {
      return false;
    }
    if (placement != query.placement) {
      return false;
    }
    return true;
  }
};

struct TransferCompleteInfo {
  CommType type;
  void *complete_handle;
};

class HixlClient {
 public:
  HixlClient(const std::string &server_ip, uint32_t server_port, const ClientConfig &config)
      : server_ip_(server_ip), server_port_(server_port), rdma_tc_(config.rdma_tc), rdma_sl_(config.rdma_sl) {};
  ~HixlClient() = default;

  Status SetLocalMemInfo(const std::vector<MemInfo> &mem_info_list);

  Status Initialize(const std::vector<EndpointConfig> &local_endpoint_list);

  Status Connect(uint32_t timeout_ms);

  Status Finalize();

  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, uint32_t timeout_ms);

  Status TransferAsync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation, TransferReq &req);

  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

 private:
  Status SendEndpointInfoReq(int32_t fd, CtrlMsgType msg_type) const;

  Status RecvEndpointInfoResp(int32_t fd, std::vector<EndpointConfig> &remote_endpoint_list) const;

  CommType ParseCommType(const std::string &local_placement, const std::string &remote_placement) const;

  bool MustUseRoce(const std::vector<EndpointConfig> &local_endpoint_list,
                   const std::vector<EndpointConfig> &remote_endpoint_list) const;

  Status TryMatchRoceEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                               const std::vector<EndpointConfig> &remote_endpoint_list,
                               std::map<CommType, HixlClientHandle> &handles);

  Status TryMatchUboeEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                               const std::vector<EndpointConfig> &remote_endpoint_list,
                               std::map<CommType, HixlClientHandle> &handles);

  Status TryMatchHccsEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                               const std::vector<EndpointConfig> &remote_endpoint_list,
                               std::map<CommType, HixlClientHandle> &handles);

  Status TryMatchUbEndpoints(const EndpointConfig &local_endpoint,
                             const std::map<MatchKey, EndpointConfig> &peer_match_endpoints,
                             std::map<CommType, bool> &expected_pairs, uint32_t &count,
                             std::map<CommType, HixlClientHandle> &handles);

  void BuildEndpointsMatchMap(const std::vector<EndpointConfig> &endpoint_list,
                              std::map<MatchKey, EndpointConfig> &peer_match_endpoints) const;

  Status FindMatchedEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                              const std::vector<EndpointConfig> &remote_endpoint_list,
                              std::map<CommType, HixlClientHandle> &handles);

  Status CreateCsClients(const EndpointConfig &local_endpoint_config, const EndpointConfig &remote_endpoint_config,
                         CommType type, std::map<CommType, HixlClientHandle> &handles);

  Status BatchTransfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                       std::vector<TransferCompleteInfo> &complete_handle_list);

  Status BatchTransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                           const std::chrono::steady_clock::time_point &sync_start, uint32_t timeout_ms);

  Status ProcessRemoteMem(uint32_t timeout_ms);

  Status UnregisterMemToCsClient(CommType type, const std::vector<MemHandle> &mem_handles);

  void WaitBatchCsSyncInflightDrain();
  Status FinalizeUnregisterAllMemHandles();
  Status FinalizeDestroyAllCsClients();
  void FinalizeClearSharedResources();

  std::string server_ip_;
  uint32_t server_port_;
  uint8_t rdma_tc_{kRdmaTrafficClass};
  uint8_t rdma_sl_{kRdmaServiceLevel};
  bool is_connected_{false};
  bool is_finalized_{false};
  bool finalize_pending_{false};
  std::atomic<int> batch_cs_sync_inflight_{0};
  std::map<TransferReq, std::vector<TransferCompleteInfo>> complete_handles_;
  std::unique_ptr<IClientHandler> client_handler_;

  std::mutex status_mutex_;
  std::mutex complete_handles_mutex_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_CLIENT_H_
