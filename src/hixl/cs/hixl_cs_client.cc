/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_cs_client.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>
#include <securec.h>
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "common/ctrl_msg_plugin.h"
#include "conn_msg_handler.h"
#include "mem_msg_handler.h"

namespace hixl {

constexpr uint32_t kFlagSizeBytes = 8;
constexpr int32_t kDefaultChannelStatus = -1;
constexpr uint32_t kWaitChannelPollIntervalMs = 1U;

Status HixlCSClient::Create(const char *server_ip, uint32_t server_port, const EndPointInfo *src_endpoint,
                            const EndPointInfo *dst_endpoint) {
  HIXL_CHECK_NOTNULL(server_ip);
  HIXL_CHECK_NOTNULL(src_endpoint);
  HIXL_CHECK_NOTNULL(dst_endpoint);
  HIXL_EVENT("[HixlClient] Create begin. Server=%s:%u. "
             "Src[Loc:%d, Proto:%d, Type:%d, Val:0x%x], "
             "Dst[Loc:%d, Proto:%d, Type:%d, Val:0x%x]",
             server_ip, server_port,
             src_endpoint->location, src_endpoint->protocol, src_endpoint->addr.type, src_endpoint->addr.id,
             dst_endpoint->location, dst_endpoint->protocol, dst_endpoint->addr.type, dst_endpoint->addr.id);
  std::lock_guard<std::mutex> lock(mutex_);
  server_ip_ = server_ip;
  server_port_ = server_port;
  src_endpoint_ = MakeShared<Endpoint>(*src_endpoint);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  Status ret = src_endpoint_->Initialize();
  HIXL_CHK_STATUS_RET(ret,
                      "[HixlClient] Failed to initialize src endpoint. "
                      "Check Config: [Loc:%d, Proto:%d, AddrVal:0x%x]",
                      src_endpoint->location, src_endpoint->protocol, src_endpoint->addr.id);
  HIXL_LOGI("[HixlClient] src_endpoint initialized. ep_handle=%p", src_endpoint_->GetHandle());
  dst_endpoint_ = *dst_endpoint;
  CtrlMsgPlugin::Initialize();
  HIXL_LOGD("[HixlClient] CtrlMsgPlugin initialized");
  EndPointHandle endpoint_handle = src_endpoint_->GetHandle();
  HIXL_EVENT("[HixlClient] Create success. server=%s:%u, src_ep_handle=%p", server_ip_.c_str(), server_port_,
             endpoint_handle);
  return SUCCESS;
}

Status HixlCSClient::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  HIXL_CHK_BOOL_RET_STATUS(dst_endpoint_.protocol != COMM_PROTOCOL_RESERVED, PARAM_INVALID,
                           "[HixlClient] Connect called but dst_endpoint is not set in Create");
  HIXL_EVENT("[HixlClient] Connect start. Target=%s:%u, timeout=%u ms", server_ip_.c_str(), server_port_, timeout_ms);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, socket_, timeout_ms),
                      "[HixlClient] Connect socket to %s:%u failed", server_ip_.c_str(), server_port_);
  HIXL_LOGI("[HixlClient] Socket connected (TCP ready). fd=%d", socket_);
  HIXL_CHK_STATUS_RET(ExchangeEndpointAndCreateChannelLocked(timeout_ms),
                      "[HixlClient] Exchange endpoint info failed. fd=%d, Target=%s:%u", socket_, server_ip_.c_str(),
                      server_port_);
  HIXL_EVENT("[HixlClient] Connect success. target=%s:%u, fd=%d, remote_ep_handle=%" PRIu64 ", ch=%p",
             server_ip_.c_str(), server_port_, socket_, dst_endpoint_handle_, client_channel_handle_);

  return SUCCESS;
}

Status WaitChannelReadyInternal(Endpoint &endpoint, ChannelHandle channel_handle, uint32_t timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    auto now = std::chrono::steady_clock::now();
    uint32_t elapsed_ms =
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
    if (elapsed_ms > timeout_ms) {
      HIXL_LOGE(TIMEOUT, "[HixlClient] Wait channel ready timeout. ch=%p, elapsed=%u ms (limit %u ms)", channel_handle,
                elapsed_ms, timeout_ms);
      return TIMEOUT;
    }
    int32_t channel_status_val = kDefaultChannelStatus;
    Status st_status = endpoint.GetChannelStatus(channel_handle, &channel_status_val);
    if (st_status != SUCCESS) {
      HIXL_LOGE(st_status, "[HixlClient] GetChannelStatus failed. ch=%p", channel_handle);
      return st_status;
    }
    if (channel_status_val == 0) {
      HIXL_LOGI("[HixlClient] Channel is ready. handle=%p, Cost=%u ms", channel_handle, elapsed_ms);
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitChannelPollIntervalMs));
  }
}

Status HixlCSClient::ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms) {
  const EndPointInfo &src_ep = src_endpoint_->GetEndpoint();
  HIXL_LOGD("[HixlClient] Sending CreateChannelReq. socket: %d, timeout: %u ms, "
            "Src[prot:%u, type:%u, id:%u], Dst[prot:%u, type:%u, id:%u]",
            socket_, timeout_ms,
            src_ep.protocol, src_ep.addr.type, src_ep.addr.id,
            dst_endpoint_.protocol, dst_endpoint_.addr.type, dst_endpoint_.addr.id);
  Status ret = ConnMsgHandler::SendCreateChannelRequest(socket_, src_ep, dst_endpoint_);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendCreateChannelRequest failed. fd=%d", socket_);
  ret = ConnMsgHandler::RecvCreateChannelResponse(socket_, dst_endpoint_handle_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvCreateChannelResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGI("[HixlClient] Connect: remote endpoint handle = %" PRIu64, dst_endpoint_handle_);
  ChannelHandle channel_handle = 0UL;
  ret = src_endpoint_->CreateChannel(dst_endpoint_, channel_handle);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] Endpoint CreateChannel failed. remote_ep_handle=%" PRIu64, dst_endpoint_handle_);
  ret = WaitChannelReadyInternal(*src_endpoint_, channel_handle, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] WaitChannelReadyInternal failed. ch=%p, timeout=%u ms", channel_handle, timeout_ms);
  client_channel_handle_ = channel_handle;
  HIXL_LOGI("[HixlClient] Channel Ready. client_channel_handle_=%p", client_channel_handle_);
  return SUCCESS;
}

Status HixlCSClient::GetRemoteMem(HcclMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num,
                                  uint32_t timeout_ms) {
  HIXL_EVENT("[HixlClient] GetRemoteMem begin. fd=%d, remote_ep_handle=%" PRIu64 ", timeout=%u ms", socket_,
             dst_endpoint_handle_, timeout_ms);
  HIXL_CHECK_NOTNULL(remote_mem_list);
  HIXL_CHECK_NOTNULL(mem_tag_list);
  HIXL_CHECK_NOTNULL(list_num);
  *remote_mem_list = nullptr;
  *mem_tag_list = nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  Status ret = MemMsgHandler::SendGetRemoteMemRequest(socket_, dst_endpoint_handle_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendGetRemoteMemRequest failed. fd=%d, remote_ep_handle=%" PRIu64, socket_,
                      dst_endpoint_handle_);
  std::vector<HixlMemDesc> mem_descs;
  ret = MemMsgHandler::RecvGetRemoteMemResponse(socket_, mem_descs, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvGetRemoteMemResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGD("[HixlClient] Recv remote mem descs success. Count=%zu", mem_descs.size());
  ret = ImportRemoteMem(mem_descs, remote_mem_list, mem_tag_list, list_num);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ImportRemoteMem failed. desc_count=%zu", mem_descs.size());
  HIXL_EVENT("[HixlClient] GetRemoteMem success. fd=%d, remote_ep_handle=%" PRIu64 ", imported=%u", socket_,
             dst_endpoint_handle_, *list_num);
  return SUCCESS;
}

namespace {
void FreeExportDesc(std::vector<HixlMemDesc> &desc_list) {
  for (auto &d : desc_list) {
    if (d.export_desc != nullptr && d.export_len > 0U) {
      std::free(d.export_desc);
      d.export_desc = nullptr;
      d.export_len = 0U;
    }
  }
  desc_list.clear();
}

Status ValidateExportDescList(const std::vector<HixlMemDesc> &desc_list) {
  for (const auto &d : desc_list) {
    if (d.export_desc == nullptr || d.export_len == 0U) {
      HIXL_LOGE(PARAM_INVALID,
                "[HixlClient] ValidateExportDescList failed! Invalid export_desc at"
                "ptr=%p, len=%u, total_count=%zu",
                d.export_desc, d.export_len, desc_list.size());
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}

Status AppendTagStorage(std::vector<std::vector<char>> &storage, const std::string &tag) {
  std::vector<char> buf(tag.size() + 1U, '\0');
  if (!tag.empty()) {
    errno_t rc = memcpy_s(buf.data(), buf.size(), tag.data(), tag.size());
    HIXL_CHK_BOOL_RET_STATUS(
        rc == EOK, FAILED,
        "[HixlClient] AppendTagStorage failed! memcpy_s error.tag: '%s', tag_len: %zu, dest_buf_size: %zu, rc: %d",
        tag.c_str(), tag.size(), buf.size(), static_cast<int32_t>(rc));
  }
  storage.emplace_back(std::move(buf));
  HIXL_LOGD("[HixlClient] AppendTagStorage success. tag: '%s', current_storage_size: %zu",
              tag.c_str(), storage.size());
  return SUCCESS;
}

void BuildTagPtrs(std::vector<std::vector<char>> &storage, std::vector<char*> &ptrs) {
  ptrs.clear();
  ptrs.reserve(storage.size());
  for (auto &s : storage) {
    ptrs.emplace_back(s.empty() ? nullptr : s.data());
  }
}

void CloseImportedBufs(EndPointHandle ep_handle, std::vector<HcommBuf> &bufs) {
  if (ep_handle == nullptr) {
    bufs.clear();
    return;
  }
  for (const auto &b : bufs) {
    HcommBuf tmp{};
    tmp.addr = b.addr;
    tmp.len = b.len;
    const HcclResult ret = HcommMemClose(ep_handle, &tmp);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGW("[HixlClient] HcommMemClose failed. addr=%p len=%" PRIu64 " ret=0x%X",
                tmp.addr, tmp.len, static_cast<uint32_t>(ret));
    }
  }
  bufs.clear();
}

Status ImportOneDesc(ImportCtx &ctx, uint32_t idx, HixlMemDesc &desc) {
  HcommBuf buf{};
  Status ret = ctx.ep->MemImport(desc.export_desc, desc.export_len, buf);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient] MemImport failed, idx=%u, tag=%s", idx, desc.tag.c_str());
    return ret;
  }
  ctx.imported.emplace_back(buf);

  HcclMem mem{};
  mem.type = desc.mem.type;
  mem.addr = desc.mem.addr;
  mem.size = desc.mem.size;
  ctx.mems.emplace_back(mem);
  ctx.tag_mem_map[desc.tag] = mem;
  HIXL_LOGD("[HixlClient] Imported mem[%u]: tag='%s', addr=%p, size=%llu", idx, desc.tag.c_str(), mem.addr, mem.size);

  return AppendTagStorage(ctx.tag_storage, desc.tag);
}

Status ImportAllDescs(ImportCtx &ctx, std::vector<HixlMemDesc> &desc_list) {
  for (uint32_t i = 0; i < ctx.num; ++i) {
    Status ret = ImportOneDesc(ctx, i, desc_list[i]);
    if (ret != SUCCESS) {
      return ret;
    }
  }
  return SUCCESS;
}

}  // namespace

void HixlCSClient::FillOutputParams(ImportCtx &ctx, HcclMem **remote_mem_list, char ***mem_tag_list,
                                    uint32_t *list_num) {
  imported_remote_bufs_ = std::move(ctx.imported);
  recorded_remote_addrs_ = std::move(ctx.recorded_addrs);
  tag_mem_descs_ = std::move(ctx.tag_mem_map);
  remote_mems_out_ = std::move(ctx.mems);
  remote_tag_storage_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_ = std::move(ctx.tag_storage);
  BuildTagPtrs(remote_tag_storage_, remote_tag_ptrs_);
  *mem_tag_list = remote_tag_ptrs_.empty() ? nullptr : remote_tag_ptrs_.data();
  *remote_mem_list = remote_mems_out_.empty() ? nullptr : remote_mems_out_.data();
  *list_num = static_cast<uint32_t>(remote_mems_out_.size());
}

Status HixlCSClient::ImportRemoteMem(std::vector<HixlMemDesc> &desc_list,
                                     HcclMem **remote_mem_list,
                                     char ***mem_tag_list,
                                     uint32_t *list_num) {
  HIXL_MAKE_GUARD(free_export_desc, [&desc_list]() {
    FreeExportDesc(desc_list);
  });
  *list_num = static_cast<uint32_t>(desc_list.size());
  Status ret = ClearRemoteMemInfo();
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ClearRemoteMemInfo before ImportRemoteMem failed");
  if (*list_num == 0U) {
    HIXL_LOGI("[HixlClient] Remote mem list is empty, nothing to import.");
    return SUCCESS;
  }
  ret = ValidateExportDescList(desc_list);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ValidateExportDescList failed");
  HIXL_CHECK_NOTNULL(src_endpoint_);
  EndPointHandle ep_handle = src_endpoint_->GetHandle();
  HIXL_CHK_BOOL_RET_STATUS(ep_handle != nullptr, FAILED, "[HixlClient] ImportRemoteMem: endpoint handle is null");
  ImportCtx ctx;
  ctx.ep = src_endpoint_.get();
  ctx.ep_handle = ep_handle;

  ctx.num = *list_num;
  ctx.imported.reserve(ctx.num);
  ctx.recorded_addrs.reserve(ctx.num);
  ctx.mems.reserve(ctx.num);
  ctx.tag_storage.reserve(ctx.num);
  ret = ImportAllDescs(ctx, desc_list);
  if (ret != SUCCESS) {
    HIXL_LOGW("[HixlClient] RollbackImport triggered. Cleaning up %zu imported bufs.", ctx.imported.size());
    CloseImportedBufs(ctx.ep_handle, ctx.imported);
    return ret;
  }
  FillOutputParams(ctx, remote_mem_list, mem_tag_list, list_num);
  return SUCCESS;
}

Status HixlCSClient::ClearRemoteMemInfo() {
  EndPointHandle ep_handle = (src_endpoint_ != nullptr) ? src_endpoint_->GetHandle() : nullptr;
  const size_t buf_cnt = imported_remote_bufs_.size();
  const size_t addr_cnt = recorded_remote_addrs_.size();
  if (buf_cnt > 0U || addr_cnt > 0U) {
    HIXL_LOGI("[HixlClient] Cleaning up remote mem info. Bufs=%zu, Addrs=%zu", buf_cnt, addr_cnt);
  }
  if (!imported_remote_bufs_.empty()) {
    if (ep_handle != nullptr) {
      CloseImportedBufs(ep_handle, imported_remote_bufs_);
    } else {
      HIXL_LOGW("[HixlClient] ClearRemoteMemInfo: endpoint handle null, skip MemClose for %zu bufs",
                imported_remote_bufs_.size());
      imported_remote_bufs_.clear();
    }
  }

  tag_mem_descs_.clear();
  remote_mems_out_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_.clear();
  return SUCCESS;
}

Status HixlCSClient::Destroy() {
  HIXL_EVENT("[HixlClient] Destroy start. fd=%d, imported_bufs=%zu, recorded_addrs=%zu", socket_,
             imported_remote_bufs_.size(), recorded_remote_addrs_.size());
  std::lock_guard<std::mutex> lock(mutex_);
  Status first_error = SUCCESS;
  Status ret = ClearRemoteMemInfo();
  if (ret != SUCCESS) {
    HIXL_LOGW("[HixlClient] ClearRemoteMemInfo failed. fd=%d, ret=%u", socket_, static_cast<uint32_t>(ret));
    if (first_error == SUCCESS) {
      first_error = ret;
    }
  }
  if (socket_ != -1) {
    HIXL_LOGI("[HixlClient] Closing socket. fd=%d", socket_);
    close(socket_);
    socket_ = -1;
  }
  if (src_endpoint_ != nullptr) {
    ret = src_endpoint_->Finalize();
    if (ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] Finalize endpoint failed in Destroy. ep_handle=%p, ret=%u",
                (src_endpoint_ != nullptr) ? src_endpoint_->GetHandle() : nullptr, static_cast<uint32_t>(ret));
      if (first_error == SUCCESS) {
        first_error = ret;
      }
    }
    src_endpoint_.reset();
  }
  HIXL_EVENT("[HixlClient] Destroy done. first_error=%u", static_cast<uint32_t>(first_error));
  return first_error;
}
}  // namespace hixl
