/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include "common/tcp_client_server.h"
#include "hixl/hixl.h"
#include "nlohmann/json.hpp"

using hixl::AscendString;
using hixl::Hixl;
using hixl::MemDesc;
using hixl::MemHandle;
using hixl::MemType;
using hixl::SUCCESS;
using hixl::TransferOp;
using hixl::TransferOpDesc;

namespace {

constexpr int32_t kMinArgCnt = 8;
constexpr int32_t kMaxArgCnt = 9;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexLocalCommRes = 7;
constexpr uint32_t kArgIndexUseBufferPool = 8;
constexpr uint64_t kTransferMemSize = 134217728;  // 128 MiB
constexpr uint32_t kBaseBlockSize = 262144;       // 0.25 MiB
constexpr uint32_t kExecuteRepeatNum = 5;
constexpr int32_t kWaitTransTimeSec = 20;
constexpr int32_t kConnectTimeoutMs = 60000;
constexpr int32_t kPortMaxValue = 65535;
constexpr uint32_t kTcpAcceptWaitSec = 30;
constexpr const char kPlacementHost[] = "host";

struct BenchmarkArgs {
  int32_t device_id = 0;
  std::string local_engine;
  std::string remote_engine;
  uint16_t tcp_port = 0;
  std::string transfer_mode;
  std::string local_comm_res;
  TransferOp transfer_op = TransferOp::READ;
  bool use_buffer_pool = false;
  bool is_client = false;
};

void PrintUsage(const char *program) {
  std::fprintf(stderr,
               "Usage: %s <device_id> <local_engine> <remote_engine> <tcp_port> h2h <read|write> "
               "<local_comm_res> [use_buffer_pool]\n"
               "  device_id is accepted for compatibility and ignored by this host-only benchmark.\n"
               "  Role is inferred from remote_engine: remote_engine with host:port means client.\n"
               "  local_comm_res is passed to HIXL Initialize() option LocalCommRes.\n",
               program);
}

bool HasPort(const std::string &engine) {
  if (engine.empty()) {
    return false;
  }
  if (engine[0] == '[') {
    const auto close = engine.find(']');
    return close != std::string::npos && close + 1U < engine.size() && engine[close + 1U] == ':';
  }
  return engine.find(':') != std::string::npos;
}

std::string ExtractTcpHost(const std::string &engine) {
  if (engine.empty()) {
    return {};
  }
  if (engine[0] == '[') {
    const auto close = engine.find(']');
    if (close == std::string::npos || close + 1U >= engine.size() || engine[close + 1U] != ':') {
      return {};
    }
    return engine.substr(1U, close - 1U);
  }
  const auto pos = engine.find(':');
  if (pos == std::string::npos || pos == 0U) {
    return {};
  }
  return engine.substr(0U, pos);
}

bool ParseInt32(const char *text, int32_t *out) {
  if (text == nullptr || out == nullptr) {
    return false;
  }
  try {
    size_t consumed = 0;
    const long value = std::stol(text, &consumed, 10);
    if (consumed != std::string(text).size()) {
      return false;
    }
    *out = static_cast<int32_t>(value);
    return value == static_cast<long>(*out);
  } catch (const std::exception &) {
    return false;
  }
}

bool ParseTcpPort(const char *text, uint16_t *out) {
  int32_t value = 0;
  if (!ParseInt32(text, &value) || value < 0 || value > kPortMaxValue || out == nullptr) {
    return false;
  }
  *out = static_cast<uint16_t>(value);
  return true;
}

bool ParseRequiredJsonString(const nlohmann::json &obj, const char *field_name, std::string *value) {
  if (field_name == nullptr || value == nullptr || !obj.contains(field_name) || !obj[field_name].is_string()) {
    std::fprintf(stderr, "[ERROR] local_comm_res endpoint missing string field: %s\n",
                 field_name == nullptr ? "<null>" : field_name);
    return false;
  }
  *value = obj[field_name].get<std::string>();
  if (value->empty()) {
    std::fprintf(stderr, "[ERROR] local_comm_res endpoint field should not be empty: %s\n", field_name);
    return false;
  }
  return true;
}

bool ValidateEndpoint(const nlohmann::json &endpoint, size_t index) {
  if (!endpoint.is_object()) {
    std::fprintf(stderr, "[ERROR] local_comm_res endpoint_list[%zu] should be object\n", index);
    return false;
  }

  std::string protocol;
  std::string comm_id;
  std::string placement;
  if (!ParseRequiredJsonString(endpoint, "protocol", &protocol) ||
      !ParseRequiredJsonString(endpoint, "comm_id", &comm_id) ||
      !ParseRequiredJsonString(endpoint, "placement", &placement)) {
    return false;
  }
  if (placement != kPlacementHost) {
    std::fprintf(stderr,
                 "[ERROR] hixl_benchmark is host-only and only accepts endpoint_list[%zu].placement=host, got: %s\n",
                 index, placement.c_str());
    return false;
  }
  if (endpoint.contains("plane") && !endpoint["plane"].is_string()) {
    std::fprintf(stderr, "[ERROR] local_comm_res endpoint_list[%zu].plane should be string when present\n", index);
    return false;
  }
  if (endpoint.contains("dst_eid") && !endpoint["dst_eid"].is_string()) {
    std::fprintf(stderr, "[ERROR] local_comm_res endpoint_list[%zu].dst_eid should be string when present\n", index);
    return false;
  }
  if (endpoint.contains("device_info") && !endpoint["device_info"].is_object()) {
    std::fprintf(stderr, "[ERROR] local_comm_res endpoint_list[%zu].device_info should be object when present\n", index);
    return false;
  }

  std::printf("[INFO] local_comm_res endpoint[%zu]: protocol=%s placement=%s has_dst_eid=%s\n", index,
              protocol.c_str(), placement.c_str(), endpoint.contains("dst_eid") ? "true" : "false");
  return true;
}

bool ParseLocalCommResArg(const std::string &local_comm_res) {
  nlohmann::json config;
  try {
    config = nlohmann::json::parse(local_comm_res);
  } catch (const nlohmann::json::exception &e) {
    std::fprintf(stderr, "[ERROR] Parse local_comm_res json failed: %s\n", e.what());
    return false;
  }

  if (!config.is_object()) {
    std::fprintf(stderr, "[ERROR] local_comm_res should be json object\n");
    return false;
  }
  if (!config.contains("net_instance_id") || !config["net_instance_id"].is_string() ||
      config["net_instance_id"].get<std::string>().empty()) {
    std::fprintf(stderr, "[ERROR] local_comm_res missing non-empty string field: net_instance_id\n");
    return false;
  }
  if (!config.contains("endpoint_list") || !config["endpoint_list"].is_array() || config["endpoint_list"].empty()) {
    std::fprintf(stderr, "[ERROR] local_comm_res missing non-empty array field: endpoint_list\n");
    return false;
  }
  if (config.contains("version") && !config["version"].is_string()) {
    std::fprintf(stderr, "[ERROR] local_comm_res.version should be string when present\n");
    return false;
  }
  if (!config.contains("version")) {
    std::printf("[WARN] local_comm_res.version is not set\n");
  }

  const auto version = config.contains("version") ? config["version"].get<std::string>() : "";
  std::printf("[INFO] local_comm_res summary: net_instance_id=%s version=%s endpoint_num=%zu\n",
              config["net_instance_id"].get<std::string>().c_str(), version.empty() ? "<missing>" : version.c_str(),
              config["endpoint_list"].size());
  for (size_t i = 0; i < config["endpoint_list"].size(); ++i) {
    if (!ValidateEndpoint(config["endpoint_list"][i], i)) {
      return false;
    }
  }
  return true;
}

bool ParseArgs(int32_t argc, char **argv, BenchmarkArgs *args) {
  if (args == nullptr || argc < kMinArgCnt || argc > kMaxArgCnt) {
    PrintUsage(argv == nullptr ? "hixl_benchmark" : argv[0]);
    return false;
  }
  if (!ParseInt32(argv[kArgIndexDeviceId], &args->device_id)) {
    std::fprintf(stderr, "[ERROR] Invalid device_id: %s\n", argv[kArgIndexDeviceId]);
    return false;
  }
  args->local_engine = argv[kArgIndexLocalEngine];
  args->remote_engine = argv[kArgIndexRemoteEngine];
  if (!ParseTcpPort(argv[kArgIndexTcpPort], &args->tcp_port)) {
    std::fprintf(stderr, "[ERROR] Invalid tcp_port: %s, should be in 0~65535\n", argv[kArgIndexTcpPort]);
    return false;
  }

  args->transfer_mode = argv[kArgIndexTransferMode];
  if (args->transfer_mode != "h2h") {
    std::fprintf(stderr, "[ERROR] hixl_benchmark only supports transfer_mode=h2h, got: %s\n",
                 args->transfer_mode.c_str());
    return false;
  }
  const std::string transfer_op = argv[kArgIndexTransferOp];
  if (transfer_op == "read") {
    args->transfer_op = TransferOp::READ;
  } else if (transfer_op == "write") {
    args->transfer_op = TransferOp::WRITE;
  } else {
    std::fprintf(stderr, "[ERROR] Invalid transfer_op: %s, expected read or write\n", transfer_op.c_str());
    return false;
  }

  args->local_comm_res = argv[kArgIndexLocalCommRes];
  if (args->local_comm_res.empty()) {
    std::fprintf(stderr, "[ERROR] local_comm_res should not be empty\n");
    return false;
  }
  if (!ParseLocalCommResArg(args->local_comm_res)) {
    return false;
  }

  if (argc == kMaxArgCnt) {
    const std::string use_buffer_pool = argv[kArgIndexUseBufferPool];
    args->use_buffer_pool = (use_buffer_pool == "true");
    if (!args->use_buffer_pool && use_buffer_pool != "false") {
      std::fprintf(stderr, "[ERROR] Invalid use_buffer_pool: %s, expected true or false\n", use_buffer_pool.c_str());
      return false;
    }
  }

  args->is_client = HasPort(args->remote_engine);
  const bool local_has_port = HasPort(args->local_engine);
  if (!args->is_client && !local_has_port) {
    std::fprintf(stderr,
                 "[ERROR] Cannot infer role: server local_engine must include host:port, or client remote_engine must "
                 "include host:port\n");
    return false;
  }
  return true;
}

std::map<AscendString, AscendString> BuildInitializeOptions(const std::string &local_comm_res, bool use_buffer_pool) {
  std::map<AscendString, AscendString> options;
  if (!use_buffer_pool) {
    options[hixl::OPTION_BUFFER_POOL] = AscendString("0:0");
  }
  options[hixl::OPTION_LOCAL_COMM_RES] = AscendString(local_comm_res.c_str());
  std::printf("[INFO] hixl_init_option %s from command line\n", hixl::OPTION_LOCAL_COMM_RES);
  return options;
}

int32_t Initialize(Hixl &hixl_engine, const std::string &local_engine, const std::string &local_comm_res,
                   bool use_buffer_pool) {
  const auto options = BuildInitializeOptions(local_comm_res, use_buffer_pool);
  const auto ret = hixl_engine.Initialize(AscendString(local_engine.c_str()), options);
  if (ret != SUCCESS) {
    std::printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  std::printf("[INFO] Initialize success local_engine=%s\n", local_engine.c_str());
  return 0;
}

int32_t RegisterMem(Hixl &hixl_engine, void *buffer, MemHandle *handle) {
  if (buffer == nullptr || handle == nullptr) {
    return -1;
  }
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(buffer);
  desc.len = static_cast<size_t>(kTransferMemSize);
  const auto ret = hixl_engine.RegisterMem(desc, MemType::MEM_HOST, *handle);
  if (ret != SUCCESS) {
    std::printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
    return -1;
  }
  std::printf("[INFO] RegisterMem success, addr:%p len=%" PRIu64 "\n", buffer, kTransferMemSize);
  return 0;
}

void Finalize(Hixl &hixl_engine, MemHandle handle) {
  if (handle != nullptr) {
    const auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != SUCCESS) {
      std::printf("[ERROR] DeregisterMem failed, ret = %u\n", ret);
    } else {
      std::printf("[INFO] DeregisterMem success\n");
    }
  }
  hixl_engine.Finalize();
  std::printf("[INFO] Finalize success\n");
}

int32_t Connect(Hixl &hixl_engine, const std::string &remote_engine) {
  const auto ret = hixl_engine.Connect(AscendString(remote_engine.c_str()), kConnectTimeoutMs);
  if (ret != SUCCESS) {
    std::printf("[ERROR] Connect failed, ret = %u\n", ret);
    return -1;
  }
  std::printf("[INFO] Connect success remote_engine=%s\n", remote_engine.c_str());
  return 0;
}

void Disconnect(Hixl &hixl_engine, const std::string &remote_engine, bool connected) {
  if (!connected) {
    return;
  }
  const auto ret = hixl_engine.Disconnect(AscendString(remote_engine.c_str()), kConnectTimeoutMs);
  if (ret != SUCCESS) {
    std::printf("[ERROR] Disconnect failed, ret = %u\n", ret);
    return;
  }
  std::printf("[INFO] Disconnect success\n");
}

int32_t Transfer(Hixl &hixl_engine, void *local_buffer, const std::string &remote_engine, uint64_t remote_addr,
                 TransferOp transfer_op) {
  const uintptr_t local_addr = reinterpret_cast<uintptr_t>(local_buffer);
  for (uint32_t i = 0; i <= kExecuteRepeatNum; ++i) {
    const uint32_t block_size = kBaseBlockSize * (1U << i);
    const uint32_t trans_num = static_cast<uint32_t>(kTransferMemSize / block_size);
    std::vector<TransferOpDesc> descs;
    descs.reserve(trans_num);
    for (uint32_t j = 0; j < trans_num; ++j) {
      const uintptr_t offset = static_cast<uintptr_t>(j) * block_size;
      TransferOpDesc desc{};
      desc.local_addr = local_addr + offset;
      desc.remote_addr = static_cast<uintptr_t>(remote_addr) + offset;
      desc.len = block_size;
      descs.emplace_back(desc);
    }

    const auto start = std::chrono::steady_clock::now();
    const auto ret =
        hixl_engine.TransferSync(AscendString(remote_engine.c_str()), transfer_op, descs, 1000 * kWaitTransTimeSec);
    if (ret != SUCCESS) {
      std::printf("[ERROR] TransferSync failed, ret = %u\n", ret);
      return -1;
    }
    const auto time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    const double time_second = static_cast<double>(time_cost) / 1000.0 / 1000.0;
    const double throughput =
        time_cost <= 0 ? 0.0 : static_cast<double>(kTransferMemSize) / 1024.0 / 1024.0 / 1024.0 / time_second;
    std::printf(
        "[INFO] Transfer success, block size: %u Bytes, transfer num: %u, time cost: %ld us, throughput: %.3lf GB/s\n",
        block_size, trans_num, static_cast<long>(time_cost), throughput);
  }
  return 0;
}

int32_t RunServer(const BenchmarkArgs &args) {
  std::printf("[INFO] server start\n");
  Hixl hixl_engine;
  MemHandle handle = nullptr;
  std::vector<uint8_t> buffer(static_cast<size_t>(kTransferMemSize), 0U);

  if (Initialize(hixl_engine, args.local_engine, args.local_comm_res, args.use_buffer_pool) != 0) {
    return -1;
  }
  if (RegisterMem(hixl_engine, buffer.data(), &handle) != 0) {
    Finalize(hixl_engine, handle);
    return -1;
  }

  TcpServerSession tcp_session(args.tcp_port, kTcpAcceptWaitSec, 1U);
  const auto addr = reinterpret_cast<uintptr_t>(buffer.data());
  if (!tcp_session.WaitAndSendAddr(addr)) {
    Finalize(hixl_engine, handle);
    return -1;
  }

  std::printf("[INFO] Wait transfer begin\n");
  if (!tcp_session.WaitAllNotify()) {
    Finalize(hixl_engine, handle);
    return -1;
  }
  std::printf("[INFO] Wait transfer end\n");

  Finalize(hixl_engine, handle);
  std::printf("[INFO] Server Sample end\n");
  return 0;
}

int32_t RunClient(const BenchmarkArgs &args) {
  std::printf("[INFO] client start\n");
  Hixl hixl_engine;
  MemHandle handle = nullptr;
  bool connected = false;
  std::vector<uint8_t> buffer(static_cast<size_t>(kTransferMemSize), 0x5AU);

  if (Initialize(hixl_engine, args.local_engine, args.local_comm_res, args.use_buffer_pool) != 0) {
    return -1;
  }
  if (RegisterMem(hixl_engine, buffer.data(), &handle) != 0) {
    Finalize(hixl_engine, handle);
    return -1;
  }

  TCPClient tcp_client;
  const std::string host = ExtractTcpHost(args.remote_engine);
  if (host.empty() || !tcp_client.ConnectToServer(host, args.tcp_port)) {
    Finalize(hixl_engine, handle);
    return -1;
  }

  uint64_t remote_addr = 0;
  if (!tcp_client.ReceiveUint64(&remote_addr)) {
    Finalize(hixl_engine, handle);
    return -1;
  }
  std::printf("[INFO] Success to receive server mem addr: 0x%" PRIx64 "\n", remote_addr);
  if (!tcp_client.ReceiveTaskStatus()) {
    Finalize(hixl_engine, handle);
    return -1;
  }
  std::printf("[INFO] Server RegisterMem success\n");

  if (Connect(hixl_engine, args.remote_engine) != 0) {
    Finalize(hixl_engine, handle);
    return -1;
  }
  connected = true;

  int32_t ret = Transfer(hixl_engine, buffer.data(), args.remote_engine, remote_addr, args.transfer_op);
  Disconnect(hixl_engine, args.remote_engine, connected);
  if (ret != 0) {
    Finalize(hixl_engine, handle);
    return -1;
  }

  if (!tcp_client.SendTaskStatus()) {
    Finalize(hixl_engine, handle);
    return -1;
  }
  tcp_client.Disconnect();

  Finalize(hixl_engine, handle);
  std::printf("[INFO] Client Sample end\n");
  return 0;
}

}  // namespace

int32_t main(int32_t argc, char **argv) {
  BenchmarkArgs args;
  if (!ParseArgs(argc, argv, &args)) {
    return -1;
  }
  std::printf(
      "[INFO] device_id=%d (ignored) role=%s local_engine=%s remote_engine=%s tcp_port=%u transfer_mode=%s "
      "transfer_op=%s use_buffer_pool=%s\n",
      args.device_id, args.is_client ? "client" : "server", args.local_engine.c_str(), args.remote_engine.c_str(),
      static_cast<unsigned>(args.tcp_port), args.transfer_mode.c_str(),
      args.transfer_op == TransferOp::READ ? "read" : "write", args.use_buffer_pool ? "true" : "false");

  if (args.is_client) {
    return RunClient(args);
  }
  return RunServer(args);
}
