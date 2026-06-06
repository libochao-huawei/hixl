/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN " "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <arpa/inet.h>
#include <map>
#include <cstring>
#include "nlohmann/json.hpp"
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "hixl/common/hixl_log.h"
#include "cs/hixl_cs.h"
#include "hixl/common/hixl_log.h"
#include "hixl/hixl_types.h"

using json = nlohmann::json;

void from_json(const json &j, EndpointLocType &l) {
  std::string s = j.get<std::string>();
  if (s == "host") {
    l = ENDPOINT_LOC_TYPE_HOST;
  } else {
    l = ENDPOINT_LOC_TYPE_DEVICE;
  }
}

void from_json(const json &j, CommProtocol &p) {
  std::string s = j.get<std::string>();
  if (s == "hccs") {
    p = COMM_PROTOCOL_HCCS;
  } else if (s == "roce") {
    p = COMM_PROTOCOL_ROCE;
  } else if (s == "UB_CTP") {
    p = COMM_PROTOCOL_UBC_CTP;
  } else if (s == "UB_TP") {
    p = COMM_PROTOCOL_UBC_TP;
  } else {
    p = COMM_PROTOCOL_RESERVED;
  }
}

void from_json(const json &j, EndpointDesc &info) {
  j.at("location").get_to(info.loc.locType);
  j.at("protocol").get_to(info.protocol);
  std::string addr;
  j.at("addr").get_to(addr);
  if (info.protocol == COMM_PROTOCOL_ROCE) {
    if (inet_pton(AF_INET, addr.c_str(), &info.commAddr.addr) == 1) {
      info.commAddr.type = COMM_ADDR_TYPE_IP_V4;
    } else if (inet_pton(AF_INET6, addr.c_str(), &info.commAddr.addr6) == 1) {
      info.commAddr.type = COMM_ADDR_TYPE_IP_V6;
    } else {
      info.commAddr.type = COMM_ADDR_TYPE_RESERVED;
    }
  }
}

namespace {
constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kClientConnectTimeoutMs = 60000;
constexpr int32_t kExpectedArgCnt = 10;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexTestType = 7;
constexpr uint32_t kArgIndexLocalCommRes = 8;
constexpr uint32_t kArgIndexRemoteCommRes = 9;
constexpr int32_t kPortMaxValue = 65535;
constexpr int32_t kBackLog = 1024;
constexpr const char *kServerMemTagName = "server_mem";
constexpr const char *kClientMemTagName = "client_mem";

constexpr uint64_t k1GB = 1ULL * 1024 * 1024 * 1024;
constexpr uint64_t k2GB = 2ULL * 1024 * 1024 * 1024;
constexpr uint64_t k4GB = 4ULL * 1024 * 1024 * 1024;
constexpr uint64_t k8GB = 8ULL * 1024 * 1024 * 1024;
constexpr uint64_t k16GB = 16ULL * 1024 * 1024 * 1024;
constexpr uint64_t k128MB = 128ULL * 1024 * 1024;

constexpr int32_t kTestTypeLargeData = 1;
constexpr int32_t kTestTypeMultiBlock = 2;

#define CHECK_ACL_RETURN(x)                                                           \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
      return __ret;                                                                   \
    }                                                                                 \
  } while (0)

struct Args {
  int32_t device_id;
  std::string local_engine;
  std::string remote_engine;
  uint16_t tcp_port;
  std::string transfer_mode;
  std::string transfer_op;
  int32_t test_type;
  std::string local_comm_res;
  std::string remote_comm_res;
};

int32_t InitEndPointInfo(const std::string &comm_res, EndpointDesc &ep) {
  try {
    ep = json::parse(comm_res).get<EndpointDesc>();
  } catch (const std::exception &e) {
    (void)printf("Failed to parse json:%s\n", e.what());
    return -1;
  }
  return 0;
}

void PrintThroughput(uint64_t size_bytes, int64_t time_us) {
  double time_second = static_cast<double>(time_us) / 1000 / 1000;
  double throughput_gb = static_cast<double>(size_bytes) / 1024 / 1024 / 1024 / time_second;
  double throughput_gbps = throughput_gb * 8;
  (void)printf("[INFO] Size: %.2f GB, Time: %.3f s, Throughput: %.3f GB/s (%.3f Gbps)\n",
               static_cast<double>(size_bytes) / 1024 / 1024 / 1024, time_second, throughput_gb, throughput_gbps);
}

int32_t TransferLargeData(HixlClientHandle client_handle, uint8_t *local_addr, 
                           const std::vector<uint64_t> &test_sizes, const std::string &transfer_op,
                           uint64_t block_size) {
  CommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, CommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  for (uint64_t transfer_size : test_sizes) {
    HIXL_LOGI("The test_sizes of this task is %u.", test_sizes);
    uint32_t num_blocks = static_cast<uint32_t>((transfer_size + block_size - 1) / block_size);
    HIXL_LOGI("The num of this task is %u.", num_blocks);
    uint64_t actual_block_size = transfer_size / num_blocks;
    HIXL_LOGI("The size of actual_block_size is %u.", actual_block_size);

    (void)printf("[INFO] TransferLargeData: size=%lu bytes, num_blocks=%u, block_size=%lu bytes\n",
                 transfer_size, num_blocks, actual_block_size);

    std::vector<HixlOneSideOpDesc> desc_list(num_blocks);
    for (uint32_t j = 0; j < num_blocks; j++) {
      desc_list[j].local_buf = local_addr + j * actual_block_size;
      desc_list[j].remote_buf = remote_addr + j * actual_block_size;
      desc_list[j].len = actual_block_size;
    }

    void *complete_handle = nullptr;
    const auto start = std::chrono::steady_clock::now();

    if (transfer_op == "write") {
      HIXL_LOGI("HixlCSClientBatchPutAsync start, num_blocks is:%u", num_blocks);
      ret = HixlCSClientBatchPutAsync(client_handle, num_blocks, desc_list.data(), &complete_handle);
    } else {
      HIXL_LOGI("HixlCSClientBatchGetAsync start, num_blocks is:%u", num_blocks);
      ret = HixlCSClientBatchGetAsync(client_handle, num_blocks, desc_list.data(), &complete_handle);
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientBatchPutAsync/HixlCSClientBatchGetAsync failed, ret = %u\n", ret);
      return -1;
    }

    HixlCompleteStatus status = HIXL_COMPLETE_STATUS_WAITING;
    while (true) {
      ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
      if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
        break;
      } else if (status == HIXL_COMPLETE_STATUS_WAITING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      if (ret != HIXL_SUCCESS) {
        (void)printf("[ERROR] HixlCSClientQueryCompleteStatus failed, ret = %u\n", ret);
        return -1;
      }
    }

    auto time_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    PrintThroughput(transfer_size, time_cost);
  }
  return 0;
}

int32_t TransferMultiBlock(HixlClientHandle client_handle, uint8_t *local_addr,const std::string &transfer_op,
                             uint32_t mem_block_count, uint64_t mem_block_size) {
  CommMem *remote_mem_list = nullptr;
  constexpr uint64_t transfer_block_size = 2ULL * 1024 * 1024;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret =
      HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, CommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
    HIXL_LOGI("the num is %u, mem_tag is %s, CommMem.type is %u, CommMem.addr is %u, CommMem.size is %u.",i ,mem_tag_list[i], remote_mem_list[i].type, remote_mem_list[i].addr, remote_mem_list[i].size);
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems["server_mem0"].addr);
  HIXL_LOGI("the remote addr is %p.", remote_addr);
  HIXL_LOGI("the local addr is %p.", local_addr);


  uint64_t total_size = mem_block_count * mem_block_size;
  uint32_t num_tasks = static_cast<uint32_t>(total_size / transfer_block_size);// 获取传输的任务块数目

  (void)printf("[INFO] MultiBlock Test: mem_block_count=%u, mem_block_size=%lu bytes, total_size=%lu bytes, "
                "transfer_block_size=%lu bytes, num_tasks=%u\n",
                mem_block_count, mem_block_size, total_size, transfer_block_size, num_tasks);

  std::vector<HixlOneSideOpDesc> desc_list(num_tasks);
  for (uint32_t j = 0; j < num_tasks; j++) {
    desc_list[j].local_buf = local_addr + j * transfer_block_size;
    desc_list[j].remote_buf = remote_addr + j * transfer_block_size;
    desc_list[j].len = transfer_block_size;
  }

  void *complete_handle = nullptr;
  const auto start = std::chrono::steady_clock::now();
  if (transfer_op == "write") {
    HIXL_LOGI("HixlCSClientBatchPutAsync start, num_blocks is:%u", num_tasks);
    ret = HixlCSClientBatchPutAsync(client_handle, num_tasks, desc_list.data(), &complete_handle);
  } else {
    HIXL_LOGI("HixlCSClientBatchGetAsync start, num_blocks is:%u", num_tasks);
    ret = HixlCSClientBatchGetAsync(client_handle, num_tasks, desc_list.data(), &complete_handle);
  }
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] Batch operation failed, ret = %u\n", ret);
    return -1;
  }

  HixlCompleteStatus status = HIXL_COMPLETE_STATUS_WAITING;
  while (true) {
    ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
    if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
      break;
    } else if (status == HIXL_COMPLETE_STATUS_WAITING) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientQueryCompleteStatus failed, ret = %u\n", ret);
      return -1;
    }
  }

  auto time_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  PrintThroughput(total_size, time_cost);
  return 0;
}

void ClientFinalize(HixlClientHandle client_handle, const std::vector<MemHandle> &handles) {
  for (auto handle : handles) {
    if (handle != nullptr) {
      HixlCSClientUnregMem(client_handle, handle);
    }
  }
  if (client_handle != nullptr) {
    HixlCSClientDestroy(client_handle);
  }
}

void ServerFinalize(HixlClientHandle server_handle, const std::vector<MemHandle> &handles) {
  for (auto handle : handles) {
    if (handle != nullptr) {
      HixlCSServerUnregMem(server_handle, handle);
    }
  }
  if (server_handle != nullptr) {
    HixlCSServerDestroy(server_handle);
  }
}

uint32_t *mem_alloc(const std::string &transfer_op, bool is_client, aclrtMemcpyKind copy_kind, CommMem mem) {
  void *tmp = nullptr;
  std::string device;
  if (is_client) {
    device = "client";
  } else {
    device = "server";
  }
  auto ret = aclrtMallocHost(&tmp, mem.size);//申请host侧的内存，用作与后续进行数据传输的内存进行交换数据
  if (ret != ACL_ERROR_NONE) {
    (void)printf("[ERROR] %s transfer_data aclrtMalloc failed, ret = %d\n", device.c_str(), ret);
    ret = aclrtFreeHost(tmp);
    return nullptr;
  }
  uint32_t *transfer_data = static_cast<uint32_t *>(tmp);
  HIXL_LOGI("The %s transfer_data addr is : %p", device.c_str(), transfer_data);
  if (transfer_data ==nullptr) {
    HIXL_LOGI("[ERROR] %s transfer_data is nullptr after malloc.", device.c_str());
    return nullptr;
  }
  // 如果是写数据，申请内存后，还需要设置内存为1，之后再复制给需要传输的内存
  HIXL_LOGI("transfer_op is %s, device type is %s.", transfer_op.c_str(), device.c_str());
  if ((transfer_op == "write" and is_client) || (transfer_op == "read" and not is_client)) {
    for (uint64_t i = 0; i < mem.size/sizeof(uint32_t); i++) {
      transfer_data[i] = 1;
    }
    HIXL_LOGI("%s write 1 to host mem.", device.c_str());
    ret = aclrtMemcpy(mem.addr, mem.size, transfer_data, mem.size, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data aclrtMemcpy failed, ret = %d\n", device.c_str(), ret);
    }
    HIXL_LOGI("The %s transfer_data have been copy to host_mem.", device.c_str());
  }
  if ((transfer_op == "read" and is_client )|| (transfer_op == "write" and not is_client)) {
    for (uint64_t i = 0; i < mem.size/sizeof(uint32_t); i++) {
      transfer_data[i] = 0;
    }
    HIXL_LOGI("%s write 0 to host mem.", device.c_str());
    ret = aclrtMemcpy(transfer_data, mem.size, mem.addr, mem.size, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data aclrtMemcpy failed, ret = %d\n", device.c_str(), ret);
    }
    HIXL_LOGI("The %s transfer_data have been copy to host_mem.", device.c_str());

    uint64_t error_num = 0;
    HIXL_LOGI("The num of this data transfer task is %u", mem.size/sizeof(uint32_t));
    for (uint64_t i = 0; i < mem.size/sizeof(uint32_t); i++) {
      if (transfer_data[i] != 1) {
        error_num++;
      }
    }
    HIXL_LOGI("The error count for this data transfer task is %u", error_num);
  }
  return transfer_data;
}

int32_t RunClientLargeData(const Args &args) {
  (void)printf("[INFO] Large Data Test Client Start\n");
  HIXL_LOGI("[INFO] Large Data Test Client Start\n");
  TCPServer tcp_server;
  if (!tcp_server.StartServer(args.tcp_port)) {
    (void)printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  (void)printf("[INFO] TCP server started.\n");
  HIXL_LOGI("[INFO] TCP server started.\n");
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }
  // 1、初始化
  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 || InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlClientHandle client_handle = nullptr;
  std::string ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  int32_t port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U));
  HixlClientDesc client_desc = {.local_endpoint = &local_ep,
                                .remote_endpoint = &remote_ep,
                                .server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
                                .tc = 0U,
                                .sl = 0U};
  HixlClientConfig client_config{};
  auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    HIXL_LOGE(ret,"[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    return -1;
  }
  // 2、注册内存地址
  uint32_t *kClientTransferData =nullptr;
  std::vector<uint64_t> test_sizes = {k2GB, k4GB, k8GB, k16GB};
  uint64_t max_size = k16GB;
  uint64_t block_size = k2GB;
  aclrtMemcpyKind copy_kind;
  MemHandle mem_handle = nullptr;
  CommMem mem{};
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");

  if (is_host) {
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    void *tmp = malloc(max_size);
    mem.addr = tmp;
    mem.type = COMM_MEM_TYPE_HOST;
    mem.size = max_size;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Client host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
    HIXL_LOGI("Client host addr malloc success mem.addr is %p, mem.size is %lu.", mem.addr, mem.size);
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, max_size, ACL_MEM_MALLOC_HUGE_ONLY);
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    }else {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }
    mem.type = COMM_MEM_TYPE_DEVICE;
    mem.size = max_size;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
      ClientFinalize(client_handle, {mem_handle});
      return -1;
    }
    HIXL_LOGI("Client device addr malloc success mem.addr is %p, mem.size is %lu.", mem.addr, mem.size);
  }
  ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientRegMem failed, ret = %u\n", ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  HIXL_LOGI("The client memory has been registered, start to copy mem");
  (void)printf("[INFO] Client memory registered, size: %lu bytes\n", max_size);
  if (args.transfer_op == "write") {
    kClientTransferData = mem_alloc(args.transfer_op,true,copy_kind,mem);
  }

  // 3、建链
  HIXL_LOGI("start to create channel.");
  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] HixlCSClientConnect failed, ret = %u\n", ret);
    return -1;
  }

  // 4、与server进行内存传输
  if (TransferLargeData(client_handle, static_cast<uint8_t *>(mem.addr), test_sizes, 
                        args.transfer_op, block_size) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  // 5.如果是读数据，传输完成后，基于copy_kind拷贝内存
  if (args.transfer_op == "read") {
    kClientTransferData = mem_alloc(args.transfer_op,true,copy_kind,mem);
  }

  // 6. 解注册，释放内存，析构
  ClientFinalize(client_handle, {mem_handle});
  auto free_ret = aclrtFreeHost(kClientTransferData);
  if (free_ret != ACL_ERROR_NONE) {
    HIXL_LOGI("kClientTransferData rtFreeHost failed, ret=%d", free_ret);
  }

  // 通过TCP通知Server侧已传输完成
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();
  (void)printf("[INFO] Large Data Test Client End\n");
  return 0;
}

int32_t RunClientMultiBlock(const Args &args) {
  (void)printf("[INFO] MultiBlock Test Client Start\n");
  // 1. 初始化
  TCPServer tcp_server;
  if (!tcp_server.StartServer(args.tcp_port)) {
    (void)printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  (void)printf("[INFO] TCP server started.\n");
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }
  // 按照不同的内存块数目创建传输任务
  constexpr uint64_t kMemBlockSize = 2ULL * 1024 * 1024;
  aclrtMemcpyKind copy_kind;
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");
  if (is_host) {
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
  } else {
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    }else {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }
  }
  uint32_t mem_block_count = 40;
  // 1、创建 HixlClientDesc 结构体
  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 || InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlClientHandle client_handle = nullptr;
  std::string ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  int32_t port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U));
  HixlClientDesc client_desc = {.local_endpoint = &local_ep,
                                .remote_endpoint = &remote_ep,
                                .server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
                                .tc = 0U,
                                .sl = 0U};
  HixlClientConfig client_config{};
  auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    return -1;
  }
  (void)printf("[INFO] ===== Test with %u memory blocks =====\n", mem_block_count);
  std::vector<uint32_t *> kClientTransferDataList(mem_block_count);
  std::vector<MemHandle> mem_handles(mem_block_count);
  std::vector<CommMem> mems(mem_block_count);
  std::vector<uint8_t *> local_addrs(mem_block_count);
  //申请一个大的内存块，之后再按照2MB划分成多个内存块
  uint64_t transfer_buffer_size = mem_block_count* kMemBlockSize;
  void * transfer_buffer_addr = nullptr;
  if (is_host) {
    transfer_buffer_addr = malloc(transfer_buffer_size);
    if (transfer_buffer_addr == nullptr) {
      (void)printf("[ERROR] Server host addr malloc failed for block %lu\n", transfer_buffer_size);
      HIXL_LOGI("[ERROR] Server host addr malloc failed for block %lu\n", transfer_buffer_size);
      ServerFinalize(client_handle, mem_handles);
      return -1;
    }
  }else {
    aclError acl_ret = aclrtMalloc(&transfer_buffer_addr, transfer_buffer_size, ACL_MEM_MALLOC_HUGE_ONLY);
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] Server aclrtMalloc failed for block %lu, ret = %d\n", transfer_buffer_size, acl_ret);
      HIXL_LOGE(acl_ret,"Server aclrtMalloc failed for block %lu.", transfer_buffer_size);
      ServerFinalize(client_handle, mem_handles);
      return -1;
    }
  }

  // 按照内存块个数完成内存注册
  for (uint32_t i = 0; i < mem_block_count; ++i) {
    if (is_host) {
      mems[i].addr = static_cast<char*>(transfer_buffer_addr) + (i * kMemBlockSize);
      mems[i].type = COMM_MEM_TYPE_HOST;
      mems[i].size = kMemBlockSize;
    } else {
      mems[i].addr = static_cast<char*>(transfer_buffer_addr) + (i * kMemBlockSize);
      mems[i].type = COMM_MEM_TYPE_DEVICE;
      mems[i].size = kMemBlockSize;
    }

    std::string mem_tag = kClientMemTagName + std::to_string(i);
    ret = HixlCSClientRegMem(client_handle, mem_tag.c_str(), &mems[i], &mem_handles[i]);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientRegMem failed for block %u, ret = %u\n", i, ret);
      ClientFinalize(client_handle, mem_handles);
      return -1;
    }
    local_addrs[i] = static_cast<uint8_t *>(mems[i].addr);
    // 写任务，提前初始化host侧的内存，之后拷贝到mem中
    if (args.transfer_op == "write") {
      kClientTransferDataList[i]= mem_alloc(args.transfer_op, true, copy_kind, mems[i]);
    }
  }
  (void)printf("[INFO] Client registered %u memory blocks, each size: %lu bytes\n", mem_block_count, kMemBlockSize);

  // 3. 建链
  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, mem_handles);
    (void)printf("[ERROR] HixlCSClientConnect failed, ret = %u\n", ret);
    return -1;
  }
  HIXL_LOGI("TransferMultiBlock start, local addr is %p.", static_cast<uint8_t *>(mems[0].addr));
  // 4. 与server进行内存传输
  if (TransferMultiBlock(client_handle, static_cast<uint8_t *>(mems[0].addr), args.transfer_op, mem_block_count, kMemBlockSize) !=0) {
    for (uint32_t i = 0; i < mem_block_count; ++i) {
      ClientFinalize(client_handle, mem_handles);
      return -1;
    }
  }

  // 5.如果是读数据，传输完成后，基于copy_kind，从mem中拷贝数据到初始化的内存中。
  if (args.transfer_op == "read") {
    for (uint32_t i = 0; i < mem_block_count; ++i) {
      kClientTransferDataList[i] = mem_alloc(args.transfer_op, true, copy_kind, mems[i]);
    }
  }

  // 6. 数据传输完成后，解链，注销client注册的内存
  ClientFinalize(client_handle, mem_handles);
  for (uint32_t i = 0; i < mem_block_count; ++i) {
    auto free_ret = aclrtFreeHost(kClientTransferDataList[i]);
    if (free_ret != ACL_ERROR_NONE) {
      HIXL_LOGI("kClientTransferDataList[%u] rtFreeHost failed, ret=%d", i, free_ret);
    }
  }
  (void)tcp_server.SendTaskStatus();
  // 7. 用例执行完之后，注销tcp服务
  tcp_server.DisConnectClient();
  tcp_server.StopServer();
  (void)printf("[INFO] MultiBlock Test Client End\n");
  return 0;
}

int32_t RunServerLargeData(const Args &args) {
  (void)printf("[INFO] Large Data Test Server Start\n");
  // 1. 初始化
  EndpointDesc ep;
  if (InitEndPointInfo(args.local_comm_res, ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlServerHandle server_handle = nullptr;
  std::string ip = args.local_engine.substr(0, args.local_engine.find(':'));
  int32_t port = std::stoi(args.local_engine.substr(args.local_engine.find(':') + 1));
  HixlServerConfig config{};
  HixlServerDesc server_desc = {.endpoint_list = &ep,
                                .server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
                                .endpoint_list_num = 1U};
  auto ret = HixlCSServerCreate(&server_desc, &config, &server_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerCreate failed, ret = %u\n", ret);
    return -1;
  }

  ret = HixlCSServerListen(server_handle, kBackLog);
  if (ret != HIXL_SUCCESS) {
    ServerFinalize(server_handle, {});
    (void)printf("[ERROR] HixlCSServerListen failed, ret = %u\n", ret);
    return -1;
  }
  (void)printf("[INFO] Server listen success, %s:%d\n", ip.c_str(), port);

  // 2. 注册内存地址
  uint32_t *kServerTransferData = nullptr;
  uint64_t max_size = k16GB;
  MemHandle mem_handle = nullptr;
  aclrtMemcpyKind copy_kind ;
  CommMem mem{};
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");

  if (is_host) {
    void *tmp = malloc(max_size);
    mem.addr = tmp;
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    mem.type = COMM_MEM_TYPE_HOST;
    mem.size = max_size;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Server host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, max_size, ACL_MEM_MALLOC_HUGE_ONLY);
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }else {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    }
    mem.type = COMM_MEM_TYPE_DEVICE;
    mem.size = max_size;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] Server aclrtMalloc failed, ret = %d\n", acl_ret);
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
  }

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerRegMem failed, ret = %u\n", ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Server memory registered, size: %lu bytes\n", max_size);

  // 3.申请host侧地址，初始化内容之后复制给第二步申请的内存
  if (args.transfer_op == "read") {
    kServerTransferData = mem_alloc(args.transfer_op,false,copy_kind,mem);
  }

  // 4. 等待client transfer
  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(args.remote_engine, args.tcp_port)) {
    return -1;
  }
  (void)printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  // 如果是写，则要再数据传输完成后再复制内存
  if (args.transfer_op == "write") {
    kServerTransferData = mem_alloc(args.transfer_op,false,copy_kind,mem);
  }
  // 5. 解注册，释放内存，析构
  ServerFinalize(server_handle, {mem_handle});
  auto free_ret = aclrtFreeHost(kServerTransferData);
  if (free_ret != ACL_ERROR_NONE) {
    HIXL_LOGI("kServerTransferData rtFreeHost failed, ret=%d", free_ret);
  }
  (void)printf("[INFO] Large Data Test Server End\n");
  return 0;
}

int32_t RunServerMultiBlock(const Args &args) {
  (void)printf("[INFO] MultiBlock Test Server Start\n");

  EndpointDesc ep;
  if (InitEndPointInfo(args.local_comm_res, ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlServerHandle server_handle = nullptr;
  std::string ip = args.local_engine.substr(0, args.local_engine.find(':'));
  int32_t port = std::stoi(args.local_engine.substr(args.local_engine.find(':') + 1));
  HixlServerConfig config{};
  HixlServerDesc server_desc = {.endpoint_list = &ep,
                                .server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
                                .endpoint_list_num = 1U};
  auto ret = HixlCSServerCreate(&server_desc, &config, &server_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerCreate failed, ret = %u\n", ret);
    return -1;
  }

  ret = HixlCSServerListen(server_handle, kBackLog);
  if (ret != HIXL_SUCCESS) {
    ServerFinalize(server_handle, {});
    (void)printf("[ERROR] HixlCSServerListen failed, ret = %u\n", ret);
    return -1;
  }
  (void)printf("[INFO] Server listen success, %s:%d\n", ip.c_str(), port);
  HIXL_LOGI("[INFO] Server listen success, %s:%d\n", ip.c_str(), port);

  constexpr uint64_t kMemBlockSize = 2ULL * 1024 * 1024;
  uint32_t mem_block_count = 40;
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");
  aclrtMemcpyKind copy_kind;
  if (is_host) {
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
  } else {
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    }else {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }
  }
  (void)printf("[INFO] ===== Test with %u memory blocks =====\n", mem_block_count);

  std::vector<MemHandle> mem_handles(mem_block_count);
  std::vector<CommMem> mems(mem_block_count);
  std::vector<uint32_t *> server_transfer_data_list(mem_block_count, nullptr);
  //申请一个大的内存块，之后再按照2MB划分成多个内存块
  uint64_t transfer_buffer_size = mem_block_count* kMemBlockSize;
  void * transfer_buffer_addr = nullptr;
  if (is_host) {

    transfer_buffer_addr = malloc(transfer_buffer_size);
    if (transfer_buffer_addr == nullptr) {
      (void)printf("[ERROR] Server host addr malloc failed for block %lu\n", transfer_buffer_size);
      HIXL_LOGI("[ERROR] Server host addr malloc failed for block %lu\n", transfer_buffer_size);
      ServerFinalize(server_handle, mem_handles);
      return -1;
    }
  }else {
    aclError acl_ret = aclrtMalloc(&transfer_buffer_addr, transfer_buffer_size, ACL_MEM_MALLOC_HUGE_ONLY);
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] Server aclrtMalloc failed for block %lu, ret = %d\n", transfer_buffer_size, acl_ret);
      HIXL_LOGE(acl_ret,"Server aclrtMalloc failed for block %lu.", transfer_buffer_size);
      ServerFinalize(server_handle, mem_handles);
      return -1;
    }
  }

  for (uint32_t i = 0; i < mem_block_count; ++i) {
    if (is_host) {
      mems[i].addr = static_cast<char*>(transfer_buffer_addr) + (i * kMemBlockSize);
      mems[i].type = COMM_MEM_TYPE_HOST;
      mems[i].size = kMemBlockSize;
    } else {
      mems[i].addr = static_cast<char*>(transfer_buffer_addr) + (i * kMemBlockSize);
      mems[i].type = COMM_MEM_TYPE_DEVICE;
      mems[i].size = kMemBlockSize;
    }

    std::string mem_tag = kServerMemTagName + std::to_string(i);
    ret = HixlCSServerRegMem(server_handle, mem_tag.c_str(), &mems[i], &mem_handles[i]);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSServerRegMem failed for block %u, ret = %u\n", i, ret);
      HIXL_LOGI("[ERROR] HixlCSServerRegMem failed for block %u, ret = %u\n", i, ret);
      ServerFinalize(server_handle, mem_handles);
      return -1;
    }
    (void)printf("[INFO] Server registered %u memory blocks, each size: %lu bytes\n", mem_block_count, kMemBlockSize);
    if (args.transfer_op == "read") {
      for (uint32_t i = 0; i < mem_block_count; ++i) {
        server_transfer_data_list[i] = mem_alloc(args.transfer_op, false, copy_kind, mems[i]);
      }
    }
  }

  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(args.remote_engine, args.tcp_port)) {
    for (uint32_t i = 0; i < mem_block_count; ++i) {
      if (server_transfer_data_list[i] != nullptr) {
        auto free_ret = aclrtFreeHost(server_transfer_data_list[i]);
        if (free_ret != ACL_ERROR_NONE) {
          HIXL_LOGI("server_transfer_data_list[%u] rtFreeHost failed, ret=%d", i, free_ret);
        }
      }
    }
    ServerFinalize(server_handle, mem_handles);
    return -1;
  }
  (void)printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  if (args.transfer_op == "write") {
    for (uint32_t i = 0; i < mem_block_count; ++i) {
      server_transfer_data_list[i] = mem_alloc(args.transfer_op, false, copy_kind, mems[i]);
    }
  }
  //注销server和给server分配的内存地址
  ServerFinalize(server_handle, mem_handles);
  for (uint32_t i = 0; i < mem_block_count; ++i) {
    if (server_transfer_data_list[i] != nullptr) {
      auto free_ret = aclrtFreeHost(server_transfer_data_list[i]);
      if (free_ret != ACL_ERROR_NONE) {
        HIXL_LOGI("server_transfer_data_list[%u] rtFreeHost failed, ret=%d", i, free_ret);
      }
    }
  }
  (void)printf("[INFO] MultiBlock Test Server End\n");
  return 0;
}

}  // namespace

void PrintUsage(const char *prog_name) {
  (void)printf("Usage: %s <device_id> <local_engine> <remote_engine> <tcp_port> <transfer_mode> "
                "<transfer_op> <test_type> <local_comm_res> <remote_comm_res>\n", prog_name);
  (void)printf("  test_type: 1=LargeData(2G/8G/32G), 2=MultiBlock(40/200/1000 blocks)\n");
  (void)printf("  Example (LargeData): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write 1 h2h h2h\n", prog_name);
  (void)printf("  Example (MultiBlock): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write 2 h2h h2h\n", prog_name);
}

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  Args args{};
  std::string device_id_str;
  std::string tcp_port_str;

  if (argc == kExpectedArgCnt) {
    device_id_str = argv[kArgIndexDeviceId];
    args.local_engine = argv[kArgIndexLocalEngine];
    args.remote_engine = argv[kArgIndexRemoteEngine];
    tcp_port_str = argv[kArgIndexTcpPort];
    args.transfer_mode = argv[kArgIndexTransferMode];
    args.transfer_op = argv[kArgIndexTransferOp];
    args.test_type = std::stoi(argv[kArgIndexTestType]);
    args.local_comm_res = argv[kArgIndexLocalCommRes];
    args.remote_comm_res = argv[kArgIndexRemoteCommRes];
    is_client = (args.remote_engine.find(':') != std::string::npos);
    (void)printf(
        "[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, transfer_mode = %s, "
        "transfer_op = %s, test_type = %d, local_comm_res = %s, remote_comm_res = %s\n",
        device_id_str.c_str(), args.local_engine.c_str(), args.remote_engine.c_str(), tcp_port_str.c_str(),
        args.transfer_mode.c_str(), args.transfer_op.c_str(), args.test_type,
        args.local_comm_res.c_str(), args.remote_comm_res.c_str());
  } else {
    (void)printf(
        "[ERROR] Expect 9 args, but got %d\n", argc - 1);
    PrintUsage(argv[0]);
    return -1;
  }

  args.device_id = std::stoi(device_id_str);
  int32_t input_tcp_port = std::stoi(tcp_port_str);
  if (input_tcp_port < 0 || input_tcp_port > kPortMaxValue) {
    (void)printf("[ERROR] Invalid port: %d, should be in 0~65535\n", input_tcp_port);
    return -1;
  }
  args.tcp_port = static_cast<uint16_t>(input_tcp_port);

  if (args.test_type != kTestTypeLargeData && args.test_type != kTestTypeMultiBlock) {
    (void)printf("[ERROR] Invalid test_type: %d (use 1=LargeData, 2=MultiBlock)\n", args.test_type);
    return -1;
  }

  CHECK_ACL_RETURN(aclrtSetDevice(args.device_id));
  (void)printf("set device success.");

  if (args.transfer_mode != "d2d" && args.transfer_mode != "h2d" && args.transfer_mode != "d2h" &&
      args.transfer_mode != "h2h") {
    (void)printf("[ERROR] Invalid value for transfer_mode: %s\n", args.transfer_mode.c_str());
    return -1;
  }

  if (args.transfer_op != "write" && args.transfer_op != "read") {
    (void)printf("[ERROR] Invalid value for transfer_op: %s\n", args.transfer_op.c_str());
    return -1;
  }

  int32_t ret = 0;
  if (args.test_type == kTestTypeLargeData) {
    if (is_client) {
      (void)printf("run client kTestTypeLargeData.");
      ret = RunClientLargeData(args);
    } else {
      (void)printf("run server kTestTypeLargeData.");
      ret = RunServerLargeData(args);
    }
  } else if (args.test_type == kTestTypeMultiBlock) {
    if (is_client) {
      (void)printf("run client kTestTypeMultiBlock.");
      ret = RunClientMultiBlock(args);
    } else {
      (void)printf("run server kTestTypeMultiBlock.");
      ret = RunServerMultiBlock(args);
    }
  }

  CHECK_ACL_RETURN(aclrtResetDevice(args.device_id));
  return ret;
}