/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem_transfer_service.h"

#include "adxl/adxl_checker.h"

namespace adxl {
namespace {
hixl::MemDesc ToHixlMemDesc(const MemDesc &mem) {
  hixl::MemDesc hixl_mem{};
  hixl_mem.addr = mem.addr;
  hixl_mem.len = mem.len;
  return hixl_mem;
}

std::vector<hixl::TransferOpDesc> ToHixlTransferOpDescs(const std::vector<TransferOpDesc> &op_descs) {
  std::vector<hixl::TransferOpDesc> hixl_op_descs;
  hixl_op_descs.reserve(op_descs.size());
  for (const auto &op_desc : op_descs) {
    hixl_op_descs.push_back({op_desc.local_addr, op_desc.remote_addr, op_desc.len});
  }
  return hixl_op_descs;
}
}  // namespace

Status FabricMemTransferService::Initialize(size_t max_stream_num, size_t task_stream_num) {
  ADXL_CHK_STATUS_RET(service_.Initialize(max_stream_num, task_stream_num, &statistic_),
                      "Failed to initialize common fabric mem transfer service.");
  ADXL_CHK_STATUS_RET(statistic_.StartPeriodicDump(), "Failed to start fabric mem statistic dump.");
  return SUCCESS;
}

void FabricMemTransferService::Finalize() {
  service_.Finalize();
  statistic_.StopPeriodicDump();
}

Status FabricMemTransferService::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  ADXL_CHK_STATUS_RET(service_.RegisterMem(ToHixlMemDesc(mem), static_cast<hixl::MemType>(type), mem_handle),
                      "Failed to register fabric mem.");
  return SUCCESS;
}

Status FabricMemTransferService::DeregisterMem(MemHandle mem_handle) {
  ADXL_CHK_STATUS_RET(service_.DeregisterMem(mem_handle), "Failed to deregister fabric mem.");
  return SUCCESS;
}

hixl::FabricMemTransferContext FabricMemTransferService::BuildContext(const ChannelPtr &channel) {
  return {channel->GetChannelId(), channel->GetStatisticChannelId(), channel->GetNewVaToOldVa()};
}

Status FabricMemTransferService::Transfer(const ChannelPtr &channel, TransferOp operation,
                                          const std::vector<TransferOpDesc> &op_descs,
                                          int32_t timeout_in_millis) {
  auto hixl_op_descs = ToHixlTransferOpDescs(op_descs);
  auto context = BuildContext(channel);
  ADXL_CHK_STATUS_RET(service_.Transfer(context, static_cast<hixl::TransferOp>(operation), hixl_op_descs,
                                        timeout_in_millis),
                      "Failed to transfer fabric mem.");
  return SUCCESS;
}

Status FabricMemTransferService::TransferAsync(const ChannelPtr &channel, TransferOp operation,
                                               const std::vector<TransferOpDesc> &op_descs, TransferReq &req) {
  auto hixl_op_descs = ToHixlTransferOpDescs(op_descs);
  auto context = BuildContext(channel);
  ADXL_CHK_STATUS_RET(service_.TransferAsync(context, static_cast<hixl::TransferOp>(operation), hixl_op_descs, req),
                      "Failed to transfer fabric mem async.");
  return SUCCESS;
}

Status FabricMemTransferService::GetTransferStatus(const ChannelPtr &channel, const TransferReq &req,
                                                   TransferStatus &status) {
  auto context = BuildContext(channel);
  hixl::TransferStatus hixl_status = hixl::TransferStatus::WAITING;
  auto ret = service_.GetTransferStatus(context, req, hixl_status);
  status = static_cast<TransferStatus>(hixl_status);
  ADXL_CHK_STATUS_RET(ret, "Failed to get fabric mem transfer status.");
  return SUCCESS;
}

std::vector<ShareHandleInfo> FabricMemTransferService::GetShareHandles() {
  return service_.GetShareHandles();
}

void FabricMemTransferService::RemoveChannel(const std::string &channel_id) {
  service_.RemoveChannel(channel_id);
  statistic_.RemoveStatisticChannel(hixl::FabricMemStatistic::GetClientStatisticChannelId(channel_id));
  statistic_.RemoveStatisticChannel(hixl::FabricMemStatistic::GetServerStatisticChannelId(channel_id));
}

hixl::FabricMemTransferService *FabricMemTransferService::GetInnerService() {
  return &service_;
}

Status FabricMemTransferService::MallocMem(MemType type, size_t size, void **ptr) {
  return hixl::FabricMemTransferService::MallocMem(static_cast<hixl::MemType>(type), size, ptr);
}

Status FabricMemTransferService::FreeMem(void *ptr) {
  return hixl::FabricMemTransferService::FreeMem(ptr);
}
}  // namespace adxl
