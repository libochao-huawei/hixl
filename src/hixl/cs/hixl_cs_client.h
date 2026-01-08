/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <array>
#include <atomic>
#include <vector>
#include <map>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "endpoint.h"
#include "channel.h"

namespace hixl {

struct ImportCtx {
  Endpoint *ep{nullptr};
  EndPointHandle ep_handle{nullptr};
  uint32_t num{0U};
  bool need_tag{false};
  std::vector<HcommBuf> imported;
  std::vector<void*> recorded_addrs;
  std::map<std::string, HcclMem> tag_mem_map;
  std::vector<HcclMem> mems;
  std::vector<std::vector<char>> tag_storage;
};

class HixlCSClient {
 public:
  HixlCSClient() = default;
  ~HixlCSClient() = default;

  Status Create(const char *server_ip, uint32_t server_port, const EndPointInfo *src_endpoint,
                const EndPointInfo *dst_endpoint);

  Status Connect(uint32_t timeout_ms);

  Status GetRemoteMem(HcclMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num, uint32_t timeout_ms);

  Status Destroy();

 private:
  Status ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms);
  Status ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, HcclMem **remote_mem_list, char ***mem_tag_list,
                         uint32_t *list_num);
  void FillOutputParams(ImportCtx &ctx, HcclMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num);
  Status ClearRemoteMemInfo();
 private:
  std::mutex mutex_;
  std::string server_ip_;
  uint32_t server_port_{0U};
  EndpointPtr src_endpoint_;
  EndPointInfo dst_endpoint_{};
  Channel client_channel_;
  ChannelHandle client_channel_handle_ = 0UL;
  uint64_t dst_endpoint_handle_{0U};
  int32_t socket_ = -1;
  std::map<std::string, HcclMem> tag_mem_descs_;
  std::vector<HcclMem> remote_mems_out_;
  std::vector<std::vector<char>> remote_tag_storage_;
  std::vector<char*> remote_tag_ptrs_;
  std::vector<void*> recorded_remote_addrs_;
  std::vector<HcommBuf> imported_remote_bufs_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
