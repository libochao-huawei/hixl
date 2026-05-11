// ----------------------------------------------------------------------------
// Copyright (c) 2025 Huawei Technologies Co., Ltd.
// This program is free software, you can redistribute it and/or modify it under the terms and conditions of
// CANN Open Software License Agreement Version 2.0 (the "License").
// Please refer to the License for details. You may not use this file except in compliance with the License.
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
// See LICENSE in the root of the software repository for the full text of the License.
// ----------------------------------------------------------------------------

// Two-rank HIXL/FabricMem D2D probe for HDK versions where aclrtMemRetainAllocationHandle fails.
//
// Usage:
//   fabric_mem_d2d_seeded_tcp <rank> <device_id> <local_engine> <remote_engine> <rank0_ip> <control_port> [bytes]
//
// Each rank allocates device memory through ACL VMM, then seeds HIXL's FabricMem VA->PA map
// before RegisterMem(..., MEM_DEVICE, ...). Rank 0 measures D2D WRITE to rank 1; rank 1
// measures D2D READ from rank 0. A TCP socket is used only for address exchange and barriers.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "hixl/hixl.h"

namespace adxl {
class FabricMemTransferService {
 public:
  static void AddVaToPaMapping(uintptr_t va_addr, aclrtDrvMemHandle pa_handle);
};
}  // namespace adxl

using namespace hixl;

namespace {
constexpr size_t kDefaultBytes = 64 * 1024 * 1024;
constexpr int kWarmup = 1;
constexpr int kIters = 5;
constexpr int kConnectTimeoutMs = 120000;
constexpr int kTransferTimeoutMs = 120000;
constexpr int kControlRetries = 300;

struct ControlMsg {
  uint64_t addr;
  uint64_t status;
};

const char *RecentErrMsg() {
  const char *msg = aclGetRecentErrMsg();
  return msg == nullptr ? "no error" : msg;
}

bool CheckAcl(aclError ret, const char *expr) {
  if (ret == ACL_ERROR_NONE) {
    return true;
  }
  std::cerr << "[ERROR] " << expr << " aclError=" << ret << " errmsg=" << RecentErrMsg() << "\n";
  return false;
}

bool SendAll(int fd, const void *buf, size_t len) {
  const auto *p = static_cast<const uint8_t *>(buf);
  while (len > 0) {
    ssize_t n = send(fd, p, len, 0);
    if (n <= 0) {
      return false;
    }
    p += n;
    len -= static_cast<size_t>(n);
  }
  return true;
}

bool RecvAll(int fd, void *buf, size_t len) {
  auto *p = static_cast<uint8_t *>(buf);
  while (len > 0) {
    ssize_t n = recv(fd, p, len, MSG_WAITALL);
    if (n <= 0) {
      return false;
    }
    p += n;
    len -= static_cast<size_t>(n);
  }
  return true;
}

bool SendMsg(int fd, uint64_t addr, uint64_t status) {
  ControlMsg msg{addr, status};
  return SendAll(fd, &msg, sizeof(msg));
}

bool RecvMsg(int fd, ControlMsg &msg) {
  return RecvAll(fd, &msg, sizeof(msg));
}

bool Barrier(int fd, int rank, uint64_t tag) {
  ControlMsg msg{};
  if (rank == 0) {
    return SendMsg(fd, 0, tag) && RecvMsg(fd, msg) && msg.status == tag;
  }
  return RecvMsg(fd, msg) && msg.status == tag && SendMsg(fd, 0, tag);
}

int CreateServerControlSocket(int port) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return -1;
  }
  int one = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    perror("bind");
    close(listen_fd);
    return -1;
  }
  if (listen(listen_fd, 1) != 0) {
    perror("listen");
    close(listen_fd);
    return -1;
  }
  int fd = accept(listen_fd, nullptr, nullptr);
  close(listen_fd);
  if (fd < 0) {
    perror("accept");
  }
  return fd;
}

int CreateClientControlSocket(const std::string &rank0_ip, int port) {
  for (int i = 0; i < kControlRetries; ++i) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      perror("socket");
      return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, rank0_ip.c_str(), &addr.sin_addr) != 1) {
      std::cerr << "[ERROR] invalid rank0_ip=" << rank0_ip << "\n";
      close(fd);
      return -1;
    }
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
      return fd;
    }
    close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cerr << "[ERROR] control connect timeout\n";
  return -1;
}

int CreateControlSocket(int rank, const std::string &rank0_ip, int port) {
  return rank == 0 ? CreateServerControlSocket(port) : CreateClientControlSocket(rank0_ip, port);
}

struct DeviceAllocation {
  void *ptr = nullptr;
  aclrtDrvMemHandle pa = nullptr;
  size_t bytes = 0;
};

bool AllocSeededDevice(int device_id, size_t bytes, DeviceAllocation &alloc) {
  alloc.bytes = bytes;
  if (!CheckAcl(aclrtReserveMemAddress(&alloc.ptr, bytes, 0, nullptr, 1), "aclrtReserveMemAddress")) {
    return false;
  }
  aclrtPhysicalMemProp prop{};
  prop.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  prop.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  prop.reserve = 0;
  prop.memAttr = ACL_MEM_P2P_HUGE;
  prop.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  prop.location.id = device_id;
  if (!CheckAcl(aclrtMallocPhysical(&alloc.pa, bytes, &prop, 0), "aclrtMallocPhysical")) {
    return false;
  }
  if (!CheckAcl(aclrtMapMem(alloc.ptr, bytes, 0, alloc.pa, 0), "aclrtMapMem")) {
    return false;
  }
  adxl::FabricMemTransferService::AddVaToPaMapping(reinterpret_cast<uintptr_t>(alloc.ptr), alloc.pa);
  return true;
}

void FreeDevice(DeviceAllocation &alloc) {
  if (alloc.ptr != nullptr) {
    (void)aclrtUnmapMem(alloc.ptr);
    if (alloc.pa != nullptr) {
      (void)aclrtFreePhysical(alloc.pa);
    }
    (void)aclrtReleaseMemAddress(alloc.ptr);
  }
  alloc.ptr = nullptr;
  alloc.pa = nullptr;
}

bool FillDevice(void *dev, size_t bytes, uint8_t value) {
  void *host = nullptr;
  if (!CheckAcl(aclrtMallocHost(&host, bytes), "aclrtMallocHost")) {
    return false;
  }
  std::fill_n(static_cast<uint8_t *>(host), bytes, value);
  bool ok = CheckAcl(aclrtMemcpy(dev, bytes, host, bytes, ACL_MEMCPY_HOST_TO_DEVICE), "aclrtMemcpy H2D");
  (void)aclrtFreeHost(host);
  return ok;
}

bool VerifyDevicePrefix(void *dev, size_t bytes, uint8_t value) {
  const size_t n = std::min(bytes, static_cast<size_t>(4096));
  void *host = nullptr;
  if (!CheckAcl(aclrtMallocHost(&host, n), "aclrtMallocHost")) {
    return false;
  }
  bool ok = CheckAcl(aclrtMemcpy(host, n, dev, n, ACL_MEMCPY_DEVICE_TO_HOST), "aclrtMemcpy D2H");
  if (ok) {
    const auto *p = static_cast<const uint8_t *>(host);
    for (size_t i = 0; i < n; ++i) {
      if (p[i] != value) {
        std::cerr << "[ERROR] verify mismatch index=" << i << " got=" << static_cast<int>(p[i])
                  << " expected=" << static_cast<int>(value) << "\n";
        ok = false;
        break;
      }
    }
  }
  (void)aclrtFreeHost(host);
  return ok;
}

double GiBs(size_t bytes, int64_t us) {
  if (us <= 0) {
    return 0.0;
  }
  constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
  return (static_cast<double>(bytes) / kGiB) / (static_cast<double>(us) / 1.0e6);
}

bool InitHixl(Hixl &engine, const std::string &local_engine) {
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  options[OPTION_BUFFER_POOL] = "0:0";
  auto ret = engine.Initialize(local_engine.c_str(), options);
  if (ret != SUCCESS) {
    std::cerr << "[ERROR] Initialize failed ret=" << ret << " errmsg=" << RecentErrMsg() << "\n";
    return false;
  }
  return true;
}

struct D2dRuntime {
  Hixl engine;
  DeviceAllocation alloc;
  MemHandle handle = nullptr;
  int fd = -1;
  uintptr_t remote_addr = 0;
};

bool RegisterDeviceMemory(D2dRuntime &runtime, size_t bytes) {
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(runtime.alloc.ptr);
  desc.len = bytes;
  auto ret = runtime.engine.RegisterMem(desc, MEM_DEVICE, runtime.handle);
  if (ret == SUCCESS) {
    return true;
  }
  std::cerr << "[ERROR] RegisterMem MEM_DEVICE failed ret=" << ret << " errmsg=" << RecentErrMsg() << "\n";
  return false;
}

bool ExchangeDeviceAddress(D2dRuntime &runtime, int rank) {
  ControlMsg peer{};
  const auto local_addr = reinterpret_cast<uintptr_t>(runtime.alloc.ptr);
  if (rank == 0 && (!RecvMsg(runtime.fd, peer) || !SendMsg(runtime.fd, local_addr, 1))) {
    return false;
  }
  if (rank == 1 && (!SendMsg(runtime.fd, local_addr, 1) || !RecvMsg(runtime.fd, peer))) {
    return false;
  }
  runtime.remote_addr = peer.addr;
  return true;
}

bool InitRuntime(D2dRuntime &runtime, int rank, int device_id, const std::string &local_engine,
                 const std::string &rank0_ip, int control_port, size_t bytes) {
  if (!CheckAcl(aclInit(nullptr), "aclInit") || !CheckAcl(aclrtSetDevice(device_id), "aclrtSetDevice")) {
    return false;
  }
  if (!InitHixl(runtime.engine, local_engine)) {
    return false;
  }
  if (!AllocSeededDevice(device_id, bytes, runtime.alloc)) {
    return false;
  }
  if (!RegisterDeviceMemory(runtime, bytes)) {
    return false;
  }
  runtime.fd = CreateControlSocket(rank, rank0_ip, control_port);
  if (runtime.fd < 0) {
    return false;
  }
  return ExchangeDeviceAddress(runtime, rank);
}

bool ConnectPeer(Hixl &engine, const std::string &remote_engine) {
  auto ret = engine.Connect(remote_engine.c_str(), kConnectTimeoutMs);
  if (ret != SUCCESS) {
    std::cerr << "[ERROR] Connect failed ret=" << ret << " errmsg=" << RecentErrMsg() << "\n";
    return false;
  }
  return true;
}

bool RunWriteTest(D2dRuntime &runtime, int rank, const std::string &remote_engine, size_t bytes) {
  if (rank == 0 && !FillDevice(runtime.alloc.ptr, bytes, 0x5a)) {
    return false;
  }
  if (!Barrier(runtime.fd, rank, 10)) {
    return false;
  }
  int64_t write_sum = 0;
  for (int it = 0; it < kWarmup + kIters; ++it) {
    if (rank == 0) {
      TransferOpDesc op{};
      op.local_addr = reinterpret_cast<uintptr_t>(runtime.alloc.ptr);
      op.remote_addr = runtime.remote_addr;
      op.len = bytes;
      auto t0 = std::chrono::steady_clock::now();
      auto ret = runtime.engine.TransferSync(remote_engine.c_str(), WRITE, {op}, kTransferTimeoutMs);
      auto t1 = std::chrono::steady_clock::now();
      if (ret != SUCCESS) {
        std::cerr << "[ERROR] D2D WRITE failed ret=" << ret << " errmsg=" << RecentErrMsg() << "\n";
        return false;
      }
      if (it >= kWarmup) {
        write_sum += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
      }
    }
    if (!Barrier(runtime.fd, rank, 20 + static_cast<uint64_t>(it))) {
      return false;
    }
  }
  if (rank == 1 && !VerifyDevicePrefix(runtime.alloc.ptr, bytes, 0x5a)) {
    return false;
  }
  if (rank == 0) {
    const int64_t avg = write_sum / kIters;
    std::cout << "[RESULT] rank=0 op=write_d2d bytes=" << bytes << " avg_us=" << avg
              << " bandwidth_GiBps=" << GiBs(bytes, avg) << "\n";
  }
  return true;
}

bool RunReadTest(D2dRuntime &runtime, int rank, const std::string &remote_engine, size_t bytes) {
  if (rank == 0 && !FillDevice(runtime.alloc.ptr, bytes, 0xa5)) {
    return false;
  }
  if (!Barrier(runtime.fd, rank, 40)) {
    return false;
  }
  int64_t read_sum = 0;
  for (int it = 0; it < kWarmup + kIters; ++it) {
    if (rank == 1) {
      TransferOpDesc op{};
      op.local_addr = reinterpret_cast<uintptr_t>(runtime.alloc.ptr);
      op.remote_addr = runtime.remote_addr;
      op.len = bytes;
      auto t0 = std::chrono::steady_clock::now();
      auto ret = runtime.engine.TransferSync(remote_engine.c_str(), READ, {op}, kTransferTimeoutMs);
      auto t1 = std::chrono::steady_clock::now();
      if (ret != SUCCESS) {
        std::cerr << "[ERROR] D2D READ failed ret=" << ret << " errmsg=" << RecentErrMsg() << "\n";
        return false;
      }
      if (it >= kWarmup) {
        read_sum += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
      }
    }
    if (!Barrier(runtime.fd, rank, 50 + static_cast<uint64_t>(it))) {
      return false;
    }
  }
  if (rank != 1) {
    return true;
  }
  if (!VerifyDevicePrefix(runtime.alloc.ptr, bytes, 0xa5)) {
    return false;
  }
  const int64_t avg = read_sum / kIters;
  std::cout << "[RESULT] rank=1 op=read_d2d bytes=" << bytes << " avg_us=" << avg
            << " bandwidth_GiBps=" << GiBs(bytes, avg) << "\n";
  return true;
}

void Cleanup(D2dRuntime &runtime, int device_id, const std::string &remote_engine) {
  if (std::getenv("FABRICMEM_SKIP_CLEANUP") != nullptr) {
    _exit(0);
  }
  (void)runtime.engine.Disconnect(remote_engine.c_str(), kConnectTimeoutMs);
  (void)runtime.engine.DeregisterMem(runtime.handle);
  runtime.engine.Finalize();
  FreeDevice(runtime.alloc);
  (void)aclrtResetDevice(device_id);
  aclFinalize();
  if (runtime.fd >= 0) {
    close(runtime.fd);
  }
}

bool Run(int rank, int device_id, const std::string &local_engine, const std::string &remote_engine,
         const std::string &rank0_ip, int control_port, size_t bytes) {
  D2dRuntime runtime;
  if (!InitRuntime(runtime, rank, device_id, local_engine, rank0_ip, control_port, bytes)) {
    return false;
  }
  if (!ConnectPeer(runtime.engine, remote_engine)) {
    return false;
  }
  if (rank == 0) {
    std::cout << "[INFO] fabric_mem_d2d_seeded_tcp bytes=" << bytes << " local=" << local_engine
              << " remote=" << remote_engine << " device=" << device_id << "\n";
  }
  if (!RunWriteTest(runtime, rank, remote_engine, bytes) || !RunReadTest(runtime, rank, remote_engine, bytes)) {
    return false;
  }
  std::cout.flush();
  Cleanup(runtime, device_id, remote_engine);
  return true;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc != 7 && argc != 8) {
    std::cerr << "Usage: " << argv[0]
              << " <rank> <device_id> <local_engine> <remote_engine> <rank0_ip> <control_port> [bytes]\n";
    return 2;
  }
  const int rank = std::stoi(argv[1]);
  const int device_id = std::stoi(argv[2]);
  const std::string local_engine = argv[3];
  const std::string remote_engine = argv[4];
  const std::string rank0_ip = argv[5];
  const int control_port = std::stoi(argv[6]);
  const size_t bytes = argc == 8 ? static_cast<size_t>(std::stoull(argv[7])) : kDefaultBytes;
  if (rank != 0 && rank != 1) {
    std::cerr << "[ERROR] rank must be 0 or 1\n";
    return 2;
  }
  return Run(rank, device_id, local_engine, remote_engine, rank0_ip, control_port, bytes) ? 0 : 1;
}
