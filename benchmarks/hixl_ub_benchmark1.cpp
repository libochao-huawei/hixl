/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "nlohmann/json.hpp"
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "common/hixl_utils.h"
#include "hixl/common/hixl_cs.h"
#include "hixl/common/hixl_inner_types.h"
#include "hixl/common/hixl_utils.h"

using json = nlohmann::json;

namespace {
constexpr int32_t kClientConnectTimeoutMs = 5000;
constexpr uint32_t kExpectedArgCnt = 7;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kTransferMemSize = 134217728;  // 128M
constexpr int32_t kPortMaxValue = 65535;
constexpr int32_t kBackLog = 1024;
constexpr const char *kServerMemTagName = "server_mem";
constexpr const int32_t kStatus = 0;
constexpr uint32_t kBlockSizes[] = {1U << 20, 2U << 20, 4U << 20, 8U << 20};

#define CHECK_ACL_RETURN(x)                                                           \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
      return __ret;                                                                   \
    }                                                                                 \
  } while (0)

struct Args {
  int32_t device_id = 0;
  std::string local_engine;
  std::string remote_engine;
  uint16_t tcp_port = 0;
  std::string transfer_mode;
  std::string transfer_op;
  std::string protocol;
  std::string mode = "single";
  uint32_t clients = 1U;
  uint32_t servers = 1U;
};

struct ClientContext {
  HixlClientHandle handle = nullptr;
  uint8_t *local_addr = nullptr;
  uint8_t *remote_addr = nullptr;
  bool is_host = false;
};

constexpr const char *kClientEndpointJsonPath = "benchmarks/ub_endpoint_client.json";
constexpr const char *kServerEndpointJsonPath = "benchmarks/ub_endpoint_server.json";

int32_t ReadEndpointJsonFile(const std::string &path, std::string &json_str) {
  std::ifstream file(path);
  if (!file.is_open()) {
    (void)printf("[ERROR] Failed to open endpoint json file: %s\n", path.c_str());
    return -1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  json_str = buffer.str();
  if (json_str.empty()) {
    (void)printf("[ERROR] Endpoint json file is empty: %s\n", path.c_str());
    return -1;
  }
  return 0;
}

int32_t ParseEndPointConfig(const std::string &comm_res, hixl::EndPointConfig &config) {
  try {
    auto config_json = json::parse(comm_res);
    config.net_instance_id = config_json.value("net_instance_id", "");
    const auto &endpoint_list = config_json.at("endpoint_list");
    if (endpoint_list.empty()) {
      (void)printf("[ERROR] endpoint_list is empty\n");
      return -1;
    }
    const auto &endpoint = endpoint_list.at(0);
    endpoint.at("protocol").get_to(config.protocol);
    endpoint.at("comm_id").get_to(config.comm_id);
    endpoint.at("dst_eid").get_to(config.dst_eid);
    endpoint.at("plane").get_to(config.plane);
    endpoint.at("placement").get_to(config.placement);
  } catch (const std::exception &e) {
    (void)printf("Failed to parse json:%s\n", e.what());
    return -1;
  }
  return 0;
}

int32_t BuildEndpointDesc(const std::string &comm_res, const Args &args, EndpointDesc &ep) {
  hixl::EndPointConfig config{};
  if (ParseEndPointConfig(comm_res, config) != 0) {
    return -1;
  }
  if (!args.protocol.empty()) {
    config.protocol = args.protocol;
  }
  auto ret = hixl::ConvertToEndPointInfo(config, ep);
  if (ret != hixl::SUCCESS) {
    (void)printf("[ERROR] ConvertToEndPointInfo failed, ret = %d\n", ret);
    return -1;
  }
  return 0;
}

int32_t BuildEndpointDescFromFile(const std::string &path, const Args &args, EndpointDesc &ep) {
  std::string json_str;
  if (ReadEndpointJsonFile(path, json_str) != 0) {
    return -1;
  }
  return BuildEndpointDesc(json_str, args, ep);
}

int32_t ParseEngine(const std::string &engine, std::string &ip, uint32_t &port) {
  auto pos = engine.find(':');
  if (pos == std::string::npos) {
    return -1;
  }
  ip = engine.substr(0U, pos);
  port = static_cast<uint32_t>(std::stoul(engine.substr(pos + 1U)));
  return 0;
}

int32_t GetRemoteAddr(HixlClientHandle client_handle, uint8_t *&remote_addr) {
  HcommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret =
      HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, HcommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);
  return 0;
}

int32_t TransferBlock(HixlClientHandle client_handle, uint8_t *local_addr, uint8_t *remote_addr, uint32_t block_size,
                      const std::string &transfer_op, int64_t &time_cost) {
  auto trans_num = kTransferMemSize / block_size;
  std::vector<const void *> local_addrs;
  std::vector<void *> remote_addrs;
  std::vector<uint64_t> lens;
  local_addrs.reserve(trans_num);
  remote_addrs.reserve(trans_num);
  lens.reserve(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    local_addrs.emplace_back(local_addr + j * block_size);
    remote_addrs.emplace_back(remote_addr + j * block_size);
    lens.emplace_back(block_size);
  }
  void *complete_handle = nullptr;
  const auto start = std::chrono::steady_clock::now();
  HixlStatus ret = HIXL_SUCCESS;
  if (transfer_op == "write") {
    ret =
        HixlCSClientBatchput(client_handle, trans_num, &remote_addrs[0], &local_addrs[0], &lens[0], &complete_handle);
  } else {
    ret =
        HixlCSClientBatchget(client_handle, trans_num, &remote_addrs[0], &local_addrs[0], &lens[0], &complete_handle);
  }
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientBatchput/HixlCSClientBatchget failed, ret = %u\n", ret);
    return -1;
  }
  int32_t status = kStatus;
  while (true) {
    ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientQueryCompleteStatus failed, ret = %u\n", ret);
      return -1;
    }
    if (status == BatchTransferStatus::COMPLETED) {
      break;
    } else {
      continue;
    }
  }
  time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  return 0;
}

void ClientFinalize(ClientContext &context) {
  if (context.handle != nullptr) {
    HixlCSClientDestroy(context.handle);
    context.handle = nullptr;
  }
  if (context.local_addr != nullptr) {
    if (context.is_host) {
      aclrtFreeHost(context.local_addr);
    } else {
      aclrtFree(context.local_addr);
    }
    context.local_addr = nullptr;
  }
}

void ServerFinalize(HixlServerHandle server_handle, MemHandle mem_handle, HcommMem &mem, bool is_host) {
  if (mem_handle != nullptr) {
    HixlCSServerUnregMem(server_handle, mem_handle);
  }
  if (server_handle != nullptr) {
    HixlCSServerDestroy(server_handle);
  }
  if (mem.addr != nullptr) {
    if (is_host) {
      aclrtFreeHost(mem.addr);
    } else {
      aclrtFree(mem.addr);
    }
  }
}

int32_t TransferSingle(const ClientContext &context, const std::string &transfer_op) {
  for (auto block_size : kBlockSizes) {
    int64_t time_cost = 0;
    if (TransferBlock(context.handle, context.local_addr, context.remote_addr, block_size, transfer_op, time_cost) != 0) {
      return -1;
    }
    double time_second = static_cast<double>(time_cost) / 1000 / 1000;
    double throughput = static_cast<double>(kTransferMemSize) / 1024 / 1024 / 1024 / time_second;
    auto trans_num = kTransferMemSize / block_size;
    (void)printf(
        "[INFO] Transfer success, block size: %u Bytes, transfer num: %u, time cost: %ld us, throughput: %.3lf GB/s\n",
        block_size, trans_num, time_cost, throughput);
  }
  return 0;
}

int32_t TransferMulti(const std::vector<ClientContext> &contexts, const std::string &transfer_op) {
  std::atomic<int32_t> ret{0};
  for (auto block_size : kBlockSizes) {
    std::vector<int64_t> time_costs(contexts.size(), 0);
    std::vector<std::thread> workers;
    workers.reserve(contexts.size());
    for (size_t i = 0; i < contexts.size(); ++i) {
      workers.emplace_back([&, i]() {
        if (TransferBlock(contexts[i].handle, contexts[i].local_addr, contexts[i].remote_addr, block_size, transfer_op,
                          time_costs[i]) != 0) {
          ret.store(-1);
        }
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
    if (ret.load() != 0) {
      return -1;
    }
    int64_t max_time = *std::max_element(time_costs.begin(), time_costs.end());
    double time_second = static_cast<double>(max_time) / 1000 / 1000;
    double total_bytes = static_cast<double>(kTransferMemSize) * contexts.size();
    double throughput = total_bytes / 1024 / 1024 / 1024 / time_second;
    auto trans_num = kTransferMemSize / block_size;
    (void)printf(
        "[INFO] Transfer success, block size: %u Bytes, transfer num: %u, time cost: %ld us, throughput: %.3lf GB/s\n",
        block_size, trans_num, max_time, throughput);
  }
  return 0;
}

int32_t RunClient(const Args &args) {
  (void)printf("[INFO] client start\n");

  std::vector<std::unique_ptr<TCPServer>> tcp_servers;
  tcp_servers.reserve(args.clients);
  for (uint32_t i = 0; i < args.clients; ++i) {
    auto server = std::make_unique<TCPServer>();
    if (!server->StartServer(static_cast<uint16_t>(args.tcp_port + i))) {
      (void)printf("[ERROR] Failed to start TCP server.\n");
      return -1;
    }
    tcp_servers.emplace_back(std::move(server));
  }
  (void)printf("[INFO] TCP server started.\n");
  for (auto &server : tcp_servers) {
    if (!server->AcceptConnection()) {
      return -1;
    }
  }

  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (BuildEndpointDescFromFile(kClientEndpointJsonPath, args, local_ep) != 0 ||
      BuildEndpointDescFromFile(kServerEndpointJsonPath, args, remote_ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  std::string remote_ip;
  uint32_t remote_port = 0U;
  if (ParseEngine(args.remote_engine, remote_ip, remote_port) != 0) {
    (void)printf("[ERROR] Invalid remote_engine: %s\n", args.remote_engine.c_str());
    return -1;
  }

  std::vector<ClientContext> contexts(args.clients);
  for (uint32_t i = 0; i < args.clients; ++i) {
    uint32_t server_index = (args.mode == "multi") ? i : 0U;
    ClientContext &context = contexts[i];
    context.is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");
    HixlStatus ret = HIXL_SUCCESS;
    ret = HixlCSClientCreate(remote_ip.c_str(), remote_port + server_index, &local_ep, &remote_ep, &context.handle);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
      for (auto &context_item : contexts) {
        ClientFinalize(context_item);
      }
      return -1;
    }
    ret = HixlCSClientConnectSync(context.handle, kClientConnectTimeoutMs);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientConnectSync failed, ret = %u\n", ret);
      for (auto &context_item : contexts) {
        ClientFinalize(context_item);
      }
      return -1;
    }
    if (context.is_host) {
      CHECK_ACL_RETURN(aclrtMallocHost(reinterpret_cast<void **>(&context.local_addr), kTransferMemSize));
    } else {
      CHECK_ACL_RETURN(aclrtMalloc(reinterpret_cast<void **>(&context.local_addr), kTransferMemSize,
                                   ACL_MEM_MALLOC_HUGE_ONLY));
    }
    if (GetRemoteAddr(context.handle, context.remote_addr) != 0) {
      for (auto &context_item : contexts) {
        ClientFinalize(context_item);
      }
      return -1;
    }
  }

  int32_t ret = 0;
  if (args.mode == "multi") {
    ret = TransferMulti(contexts, args.transfer_op);
  } else {
    ret = TransferSingle(contexts[0], args.transfer_op);
  }
  if (ret != 0) {
    for (auto &context : contexts) {
      ClientFinalize(context);
    }
    return -1;
  }

  for (auto &context : contexts) {
    ClientFinalize(context);
  }

  for (auto &server : tcp_servers) {
    (void)server->SendTaskStatus();
    server->DisConnectClient();
    server->StopServer();
  }
  (void)printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const Args &args) {
  (void)printf("[INFO] server start\n");

  EndpointDesc ep;
  if (BuildEndpointDescFromFile(kServerEndpointJsonPath, args, ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  std::string local_ip;
  uint32_t base_port = 0U;
  if (ParseEngine(args.local_engine, local_ip, base_port) != 0) {
    (void)printf("[ERROR] Invalid local_engine: %s\n", args.local_engine.c_str());
    return -1;
  }

  std::vector<HixlServerHandle> server_handles(args.servers, nullptr);
  std::vector<MemHandle> mem_handles(args.servers, nullptr);
  std::vector<HcommMem> mems(args.servers);
  std::vector<bool> mem_is_host(args.servers, false);
  for (uint32_t i = 0; i < args.servers; ++i) {
    HixlServerConfig config{};
    auto ret = HixlCSServerCreate(local_ip.c_str(), base_port + i, &ep, 1U, &config, &server_handles[i]);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSServerCreate failed, ret = %u\n", ret);
      return -1;
    }

    ret = HixlCSServerListen(server_handles[i], kBackLog);
    if (ret != HIXL_SUCCESS) {
      ServerFinalize(server_handles[i], mem_handles[i], mems[i], mem_is_host[i]);
      (void)printf("[ERROR] HixlCSServerListen failed, ret = %u\n", ret);
      return -1;
    }
    (void)printf("[INFO] Server listen success, %s:%u\n", local_ip.c_str(), base_port + i);

    mem_is_host[i] = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");
    aclError acl_ret = ACL_ERROR_NONE;
    if (mem_is_host[i]) {
      acl_ret = aclrtMallocHost(&mems[i].addr, kTransferMemSize);
      mems[i].type = HCCL_MEM_TYPE_HOST;
    } else {
      acl_ret = aclrtMalloc(&mems[i].addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
      mems[i].type = HCCL_MEM_TYPE_DEVICE;
    }
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
      ServerFinalize(server_handles[i], mem_handles[i], mems[i], mem_is_host[i]);
      return -1;
    }

    ret = HixlCSServerRegMem(server_handles[i], kServerMemTagName, &mems[i], &mem_handles[i]);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSServerRegMem failed, ret = %u\n", ret);
      ServerFinalize(server_handles[i], mem_handles[i], mems[i], mem_is_host[i]);
      return -1;
    }
    (void)printf("[INFO] RegisterMem success\n");
  }

  std::vector<TCPClient> tcp_clients(args.servers);
  for (uint32_t i = 0; i < args.servers; ++i) {
    if (!tcp_clients[i].ConnectToServer(args.remote_engine, static_cast<uint16_t>(args.tcp_port + i))) {
      return -1;
    }
  }
  (void)printf("[INFO] Wait transfer begin\n");
  for (uint32_t i = 0; i < args.servers; ++i) {
    if (tcp_clients[i].ReceiveTaskStatus()) {
      (void)printf("[INFO] Wait transfer end\n");
    }
    tcp_clients[i].Disconnect();
  }

  for (uint32_t i = 0; i < args.servers; ++i) {
    ServerFinalize(server_handles[i], mem_handles[i], mems[i], mem_is_host[i]);
  }
  (void)printf("[INFO] Server Sample end\n");
  return 0;
}

int32_t ParseArgs(int32_t argc, char **argv, Args &args, bool &is_client) {
  if (argc < static_cast<int32_t>(kExpectedArgCnt)) {
    (void)printf(
        "[ERROR] Expect 6 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, transfer_op) with "
        "options, but got %d\n",
        argc - 1);
    return -1;
  }
  args.local_engine = argv[kArgIndexLocalEngine];
  args.remote_engine = argv[kArgIndexRemoteEngine];
  args.transfer_mode = argv[kArgIndexTransferMode];
  args.transfer_op = argv[kArgIndexTransferOp];
  args.device_id = std::stoi(argv[kArgIndexDeviceId]);
  int32_t input_tcp_port = std::stoi(argv[kArgIndexTcpPort]);
  if (input_tcp_port < 0 || input_tcp_port > kPortMaxValue) {
    (void)printf("[ERROR] Invalid port: %d, should be in 0~65535\n", input_tcp_port);
    return -1;
  }
  args.tcp_port = static_cast<uint16_t>(input_tcp_port);

  for (int32_t i = static_cast<int32_t>(kExpectedArgCnt); i < argc; ++i) {
    std::string opt = argv[i];
    if (opt == "--protocol" && i + 1 < argc) {
      args.protocol = argv[++i];
    } else if (opt == "--mode" && i + 1 < argc) {
      args.mode = argv[++i];
    } else if (opt == "--clients" && i + 1 < argc) {
      args.clients = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (opt == "--servers" && i + 1 < argc) {
      args.servers = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else {
      (void)printf("[ERROR] Unknown or incomplete option: %s\n", opt.c_str());
      return -1;
    }
  }

  if (args.protocol.empty()) {
    (void)printf("[ERROR] --protocol ub_ctp|ub_tp is required\n");
    return -1;
  }
  if (args.mode != "single" && args.mode != "multi") {
    (void)printf("[ERROR] Invalid value for mode: %s\n", args.mode.c_str());
    return -1;
  }
  if (args.mode == "single") {
    args.clients = 1U;
    args.servers = 1U;
  } else if (args.clients != args.servers) {
    (void)printf("[ERROR] Multi mode requires clients == servers\n");
    return -1;
  }
  if (args.clients == 0U || args.servers == 0U) {
    (void)printf("[ERROR] clients/servers must be > 0\n");
    return -1;
  }

  is_client = (args.remote_engine.find(':') != std::string::npos);
  (void)printf(
      "[INFO] device_id = %d, local_engine = %s, remote_engine = %s, tcp_port = %u, transfer_mode = %s, "
      "transfer_op = %s, protocol = %s, mode = %s, clients = %u, servers = %u\n",
      args.device_id, args.local_engine.c_str(), args.remote_engine.c_str(), args.tcp_port, args.transfer_mode.c_str(),
      args.transfer_op.c_str(), args.protocol.c_str(), args.mode.c_str(), args.clients, args.servers);
  return 0;
}
}  // namespace

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  Args args{};
  if (ParseArgs(argc, argv, args, is_client) != 0) {
    return -1;
  }
  CHECK_ACL_RETURN(aclrtSetDevice(args.device_id));

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
  if (is_client) {
    ret = RunClient(args);
  } else {
    ret = RunServer(args);
  }
  CHECK_ACL_RETURN(aclrtResetDevice(args.device_id));
  return ret;
}