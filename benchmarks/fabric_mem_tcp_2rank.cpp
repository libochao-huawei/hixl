// ----------------------------------------------------------------------------
// Copyright (c) 2025 Huawei Technologies Co., Ltd.
// This program is free software, you can redistribute it and/or modify it under the terms and conditions of
// CANN Open Software License Agreement Version 2.0 (the "License").
// Please refer to the License for details. You may not use this file except in compliance with the License.
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
// See LICENSE in the root of the software repository for the full text of the License.
// ----------------------------------------------------------------------------

// Two-rank FabricMem probe for A3 HIXL when shared filesystem barriers are unavailable.
//
// Usage:
//   fabric_mem_tcp_2rank <rank> <device_id> <local_engine> <remote_engine> <rank0_ip> <control_port>
//
// Rank 0 listens on control_port. Rank 1 connects to rank0_ip:control_port.
// Both ranks allocate/register MEM_HOST via AdxlEngine::MallocMem, exchange host VAs over TCP,
// then rank 0 measures D2RH WRITE and rank 1 measures RH2D READ.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "adxl/adxl_engine.h"
#include "hixl/hixl_types.h"

using namespace adxl;

namespace {
constexpr uint32_t kLayers = 61U;
constexpr uint32_t kLargeChunkBytes = 128U * 1024U;
constexpr uint32_t kSmallChunkBytes = 16U * 1024U;
constexpr size_t kBlockBytes = static_cast<size_t>(kLayers) * (kLargeChunkBytes + kSmallChunkBytes);
constexpr uint64_t kBytesPerGiB = 1024ULL * 1024ULL * 1024ULL;
constexpr int32_t kConnectTimeoutMs = 120000;
constexpr int32_t kTransferTimeoutMs = 120000;
constexpr int kWarmupIterations = 1;
constexpr int kTimedIterations = 10;
constexpr int kBlockCounts[] = {16, 32, 48, 64};
constexpr size_t kMaxTotalBlocks = 64U;
constexpr size_t kPoolBytes = kBytesPerGiB;
constexpr int kControlRetries = 300;

#define CHECK_ACL(x)                                                                 \
  do {                                                                               \
    aclError acl_ret = (x);                                                          \
    if (acl_ret != ACL_ERROR_NONE) {                                                 \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << acl_ret << "\n"; \
      return false;                                                                  \
    }                                                                                \
  } while (0)

struct ControlMsg {
  uint64_t host_va;
  uint64_t status;
};

const char *AclErrMsg() {
  const char *msg = aclGetRecentErrMsg();
  return msg == nullptr ? "no error" : msg;
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

bool SendMsg(int fd, uint64_t host_va, uint64_t status) {
  ControlMsg msg{host_va, status};
  return SendAll(fd, &msg, sizeof(msg));
}

bool RecvMsg(int fd, ControlMsg &msg) {
  return RecvAll(fd, &msg, sizeof(msg));
}

int CreateControlSocket(int rank, const std::string &rank0_ip, int port) {
  if (rank == 0) {
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
      std::cerr << "[ERROR] invalid rank0_ip: " << rank0_ip << "\n";
      close(fd);
      return -1;
    }
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
      return fd;
    }
    close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cerr << "[ERROR] connect control socket timeout\n";
  return -1;
}

bool AllocateDeviceBuffer(int32_t device_id, size_t size, void *&va, aclrtDrvMemHandle &pa_handle) {
  CHECK_ACL(aclrtReserveMemAddress(&va, size, 0, nullptr, 1));
  aclrtPhysicalMemProp prop{};
  prop.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  prop.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  prop.reserve = 0;
  prop.memAttr = ACL_HBM_MEM_HUGE;
  prop.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  prop.location.id = device_id;
  CHECK_ACL(aclrtMallocPhysical(&pa_handle, size, &prop, 0));
  CHECK_ACL(aclrtMapMem(va, size, 0, pa_handle, 0));
  return true;
}

void FreeDeviceBuffer(void *va, aclrtDrvMemHandle pa_handle) {
  if (va != nullptr) {
    (void)aclrtUnmapMem(va);
    (void)aclrtFreePhysical(pa_handle);
    (void)aclrtReleaseMemAddress(va);
  }
}

void FillPattern(uint8_t *base, size_t len, int seed) {
  for (size_t i = 0; i < len; ++i) {
    base[i] = static_cast<uint8_t>((seed + static_cast<int>(i)) & 0xFF);
  }
}

bool CheckPattern(const uint8_t *base, size_t len, int seed) {
  for (size_t i = 0; i < len; ++i) {
    uint8_t expected = static_cast<uint8_t>((seed + static_cast<int>(i)) & 0xFF);
    if (base[i] != expected) {
      std::cerr << "[ERROR] verify mismatch at byte " << i << " got=" << static_cast<int>(base[i])
                << " expected=" << static_cast<int>(expected) << "\n";
      return false;
    }
  }
  return true;
}

double GbpsFromBytesAndUs(uint64_t total_bytes, int64_t time_us) {
  if (time_us <= 0) {
    return 0.0;
  }
  const double sec = static_cast<double>(time_us) / 1.0e6;
  return static_cast<double>(total_bytes) / static_cast<double>(kBytesPerGiB) / sec;
}

bool InitEngine(AdxlEngine &engine, const std::string &local_engine) {
  std::map<AscendString, AscendString> options;
  options[AscendString(hixl::OPTION_ENABLE_USE_FABRIC_MEM)] = AscendString("1");
  options[AscendString(hixl::OPTION_BUFFER_POOL)] = AscendString("0:0");
  Status st = engine.Initialize(AscendString(local_engine.c_str()), options);
  if (st != SUCCESS) {
    std::cerr << "[ERROR] AdxlEngine::Initialize failed ret=" << st << " " << AclErrMsg() << "\n";
    return false;
  }
  return true;
}

bool Run(int rank, int32_t device_id, const std::string &local_engine, const std::string &remote_engine,
         const std::string &rank0_ip, int control_port) {
  CHECK_ACL(aclInit(nullptr));
  CHECK_ACL(aclrtSetDevice(device_id));

  AdxlEngine engine;
  void *host_raw = nullptr;
  void *dev_raw = nullptr;
  aclrtDrvMemHandle dev_pa{};
  MemHandle host_handle = nullptr;

  if (!InitEngine(engine, local_engine)) {
    return false;
  }
  if (AdxlEngine::MallocMem(MemType::MEM_HOST, kPoolBytes, &host_raw) != SUCCESS) {
    std::cerr << "[ERROR] AdxlEngine::MallocMem MEM_HOST failed\n";
    return false;
  }
  auto *host_pool = reinterpret_cast<uint8_t *>(host_raw);
  if (!AllocateDeviceBuffer(device_id, kPoolBytes, dev_raw, dev_pa)) {
    return false;
  }
  auto *dev_pool = reinterpret_cast<uint8_t *>(dev_raw);

  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(host_pool);
  desc.len = kPoolBytes;
  if (engine.RegisterMem(desc, MEM_HOST, host_handle) != SUCCESS) {
    std::cerr << "[ERROR] RegisterMem MEM_HOST failed " << AclErrMsg() << "\n";
    return false;
  }

  int control_fd = CreateControlSocket(rank, rank0_ip, control_port);
  if (control_fd < 0) {
    return false;
  }

  ControlMsg peer_msg{};
  if (rank == 0) {
    if (!RecvMsg(control_fd, peer_msg) || !SendMsg(control_fd, reinterpret_cast<uintptr_t>(host_pool), 0)) {
      std::cerr << "[ERROR] control exchange failed on rank 0\n";
      return false;
    }
  } else {
    if (!SendMsg(control_fd, reinterpret_cast<uintptr_t>(host_pool), 0) || !RecvMsg(control_fd, peer_msg)) {
      std::cerr << "[ERROR] control exchange failed on rank 1\n";
      return false;
    }
  }
  const uint64_t remote_host_va = peer_msg.host_va;

  if (engine.Connect(AscendString(remote_engine.c_str()), kConnectTimeoutMs) != SUCCESS) {
    std::cerr << "[ERROR] Connect failed remote=" << remote_engine << " " << AclErrMsg() << "\n";
    return false;
  }
  if (rank == 0) {
    std::cout << "[INFO] fabric_mem_tcp_2rank block_bytes=" << kBlockBytes
              << " pool_GiB=" << (static_cast<double>(kPoolBytes) / static_cast<double>(kBytesPerGiB))
              << " local_engine=" << local_engine << " remote_engine=" << remote_engine << "\n";
  }

  for (int blocks : kBlockCounts) {
    const size_t bytes = static_cast<size_t>(blocks) * kBlockBytes;
    int64_t write_us_sum = 0;
    int64_t read_us_sum = 0;
    const int seed = 17 + blocks;

    if (rank == 0) {
      FillPattern(host_pool, bytes, seed);
      CHECK_ACL(aclrtMemcpy(dev_pool, bytes, host_pool, bytes, ACL_MEMCPY_HOST_TO_DEVICE));
    }
    if (!SendMsg(control_fd, 0, 100 + static_cast<uint64_t>(blocks)) || !RecvMsg(control_fd, peer_msg)) {
      std::cerr << "[ERROR] pre-write barrier failed\n";
      return false;
    }

    for (int it = 0; it < kWarmupIterations + kTimedIterations; ++it) {
      if (rank == 0) {
        TransferOpDesc op{};
        op.local_addr = reinterpret_cast<uintptr_t>(dev_pool);
        op.remote_addr = remote_host_va;
        op.len = bytes;
        auto t0 = std::chrono::steady_clock::now();
        Status st = engine.TransferSync(AscendString(remote_engine.c_str()), WRITE, {op}, kTransferTimeoutMs);
        auto t1 = std::chrono::steady_clock::now();
        if (st != SUCCESS) {
          std::cerr << "[ERROR] D2RH WRITE failed ret=" << st << "\n";
          return false;
        }
        if (it >= kWarmupIterations) {
          write_us_sum += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        }
      }
      if (!SendMsg(control_fd, 0, 200 + static_cast<uint64_t>(it)) || !RecvMsg(control_fd, peer_msg)) {
        std::cerr << "[ERROR] post-write barrier failed\n";
        return false;
      }
    }

    if (rank == 1 && !CheckPattern(host_pool, bytes < 4096 ? bytes : 4096, seed)) {
      return false;
    }

    for (int it = 0; it < kWarmupIterations + kTimedIterations; ++it) {
      if (rank == 1) {
        TransferOpDesc op{};
        op.local_addr = reinterpret_cast<uintptr_t>(dev_pool);
        op.remote_addr = remote_host_va;
        op.len = bytes;
        auto t0 = std::chrono::steady_clock::now();
        Status st = engine.TransferSync(AscendString(remote_engine.c_str()), READ, {op}, kTransferTimeoutMs);
        auto t1 = std::chrono::steady_clock::now();
        if (st != SUCCESS) {
          std::cerr << "[ERROR] RH2D READ failed ret=" << st << "\n";
          return false;
        }
        if (it >= kWarmupIterations) {
          read_us_sum += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        }
      }
      if (!SendMsg(control_fd, 0, 300 + static_cast<uint64_t>(it)) || !RecvMsg(control_fd, peer_msg)) {
        std::cerr << "[ERROR] post-read barrier failed\n";
        return false;
      }
    }

    if (rank == 1) {
      CHECK_ACL(aclrtMemcpy(host_pool, 4096, dev_pool, 4096, ACL_MEMCPY_DEVICE_TO_HOST));
      if (!CheckPattern(host_pool, 4096, seed)) {
        return false;
      }
    }

    if (rank == 0) {
      const int64_t avg_write = write_us_sum / kTimedIterations;
      std::cout << "[RESULT] rank=0 blocks=" << blocks << " d2rh_write_time_avg_us=" << avg_write
                << " d2rh_write_bandwidth_GBps=" << GbpsFromBytesAndUs(bytes, avg_write) << "\n";
    } else {
      const int64_t avg_read = read_us_sum / kTimedIterations;
      std::cout << "[RESULT] rank=1 blocks=" << blocks << " rh2d_read_time_avg_us=" << avg_read
                << " rh2d_read_bandwidth_GBps=" << GbpsFromBytesAndUs(bytes, avg_read) << "\n";
    }
    std::cout.flush();
  }

  if (std::getenv("FABRICMEM_SKIP_CLEANUP") != nullptr) {
    std::cout << "[INFO] FABRICMEM_SKIP_CLEANUP set; exiting after verified benchmark results\n";
    std::cout.flush();
    _exit(0);
  }

  (void)engine.Disconnect(AscendString(remote_engine.c_str()), kConnectTimeoutMs);
  (void)engine.DeregisterMem(host_handle);
  FreeDeviceBuffer(dev_raw, dev_pa);
  (void)AdxlEngine::FreeMem(host_raw);
  engine.Finalize();
  (void)aclrtResetDevice(device_id);
  aclFinalize();
  close(control_fd);
  return true;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc != 7) {
    std::cerr << "Usage: " << argv[0]
              << " <rank> <device_id> <local_engine> <remote_engine> <rank0_ip> <control_port>\n";
    return 1;
  }
  const int rank = std::stoi(argv[1]);
  if (rank != 0 && rank != 1) {
    std::cerr << "[ERROR] rank must be 0 or 1\n";
    return 1;
  }
  const int32_t device_id = std::stoi(argv[2]);
  const std::string local_engine = argv[3];
  const std::string remote_engine = argv[4];
  const std::string rank0_ip = argv[5];
  const int control_port = std::stoi(argv[6]);
  return Run(rank, device_id, local_engine, remote_engine, rank0_ip, control_port) ? 0 : 1;
}
