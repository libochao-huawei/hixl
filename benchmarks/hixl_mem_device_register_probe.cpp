// ----------------------------------------------------------------------------
// Copyright (c) 2025 Huawei Technologies Co., Ltd.
// This program is free software, you can redistribute it and/or modify it under the terms and conditions of
// CANN Open Software License Agreement Version 2.0 (the "License").
// Please refer to the License for details. You may not use this file except in compliance with the License.
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
// See LICENSE in the root of the software repository for the full text of the License.
// ----------------------------------------------------------------------------

// Probe HIXL/FabricMem MEM_DEVICE registration with multiple device allocation methods.
//
// Usage:
//   hixl_mem_device_register_probe <device_id> <local_engine> <mode> [bytes]
//
// Modes:
//   acl_huge_only       aclrtMalloc(..., ACL_MEM_MALLOC_HUGE_ONLY)
//   acl_hbw_huge_first  aclrtMalloc(..., ACL_MEM_TYPE_HIGH_BAND_WIDTH | ACL_MEM_MALLOC_HUGE_FIRST)
//   vmm_hbm_huge        aclrtReserveMemAddress + aclrtMallocPhysical(DEVICE, ACL_HBM_MEM_HUGE) + aclrtMapMem
//   vmm_p2p_huge        aclrtReserveMemAddress + aclrtMallocPhysical(DEVICE, ACL_MEM_P2P_HUGE) + aclrtMapMem

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;

namespace {
constexpr size_t kDefaultBytes = 2 * 1024 * 1024;

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

struct Allocation {
  void *ptr = nullptr;
  aclrtDrvMemHandle pa = nullptr;
  bool is_vmm = false;
  std::string mode;
};

bool AllocAcl(size_t bytes, aclrtMemMallocPolicy policy, Allocation &alloc) {
  alloc.is_vmm = false;
  return CheckAcl(aclrtMalloc(&alloc.ptr, bytes, policy), "aclrtMalloc");
}

bool AllocVmm(int device_id, size_t bytes, aclrtMemAttr mem_attr, Allocation &alloc) {
  alloc.is_vmm = true;
  if (!CheckAcl(aclrtReserveMemAddress(&alloc.ptr, bytes, 0, nullptr, 1), "aclrtReserveMemAddress")) {
    return false;
  }
  aclrtPhysicalMemProp prop{};
  prop.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  prop.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  prop.reserve = 0;
  prop.memAttr = mem_attr;
  prop.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  prop.location.id = device_id;
  if (!CheckAcl(aclrtMallocPhysical(&alloc.pa, bytes, &prop, 0), "aclrtMallocPhysical")) {
    return false;
  }
  return CheckAcl(aclrtMapMem(alloc.ptr, bytes, 0, alloc.pa, 0), "aclrtMapMem");
}

void FreeAllocation(Allocation &alloc) {
  if (alloc.ptr == nullptr) {
    return;
  }
  if (alloc.is_vmm) {
    (void)aclrtUnmapMem(alloc.ptr);
    if (alloc.pa != nullptr) {
      (void)aclrtFreePhysical(alloc.pa);
    }
    (void)aclrtReleaseMemAddress(alloc.ptr);
  } else {
    (void)aclrtFree(alloc.ptr);
  }
  alloc.ptr = nullptr;
  alloc.pa = nullptr;
}

bool Allocate(int device_id, const std::string &mode, size_t bytes, Allocation &alloc) {
  alloc.mode = mode;
  if (mode == "acl_huge_only") {
    return AllocAcl(bytes, ACL_MEM_MALLOC_HUGE_ONLY, alloc);
  }
  if (mode == "acl_hbw_huge_first") {
    auto policy = static_cast<aclrtMemMallocPolicy>(
        static_cast<uint32_t>(ACL_MEM_TYPE_HIGH_BAND_WIDTH) | static_cast<uint32_t>(ACL_MEM_MALLOC_HUGE_FIRST));
    return AllocAcl(bytes, policy, alloc);
  }
  if (mode == "vmm_hbm_huge") {
    return AllocVmm(device_id, bytes, ACL_HBM_MEM_HUGE, alloc);
  }
  if (mode == "vmm_p2p_huge") {
    return AllocVmm(device_id, bytes, ACL_MEM_P2P_HUGE, alloc);
  }
  std::cerr << "[ERROR] unknown mode=" << mode << "\n";
  return false;
}

struct ProbeArgs {
  int device_id;
  std::string local_engine;
  std::string mode;
  size_t bytes;
};

bool ParseArgs(int argc, char **argv, ProbeArgs &args) {
  if (argc != 4 && argc != 5) {
    std::cerr << "Usage: " << argv[0] << " <device_id> <local_engine> <mode> [bytes]\n";
    return false;
  }
  args.device_id = std::stoi(argv[1]);
  args.local_engine = argv[2];
  args.mode = argv[3];
  args.bytes = argc == 5 ? static_cast<size_t>(std::stoull(argv[4])) : kDefaultBytes;
  return true;
}

void Cleanup(Hixl &hixl_engine, Allocation &alloc, int device_id) {
  hixl_engine.Finalize();
  FreeAllocation(alloc);
  (void)aclrtResetDevice(device_id);
  aclFinalize();
}

bool InitRuntime(const ProbeArgs &args, Hixl &hixl_engine) {
  if (!CheckAcl(aclInit(nullptr), "aclInit") || !CheckAcl(aclrtSetDevice(args.device_id), "aclrtSetDevice")) {
    return false;
  }
  std::map<AscendString, AscendString> options;
  options[OPTION_ENABLE_USE_FABRIC_MEM] = "1";
  options[OPTION_BUFFER_POOL] = "0:0";
  auto ret = hixl_engine.Initialize(args.local_engine.c_str(), options);
  if (ret != SUCCESS) {
    std::cerr << "[ERROR] Initialize failed ret=" << ret << " errmsg=" << RecentErrMsg() << "\n";
    return false;
  }
  std::cout << "[INFO] Initialize success device=" << args.device_id << " engine=" << args.local_engine
            << " mode=" << args.mode << " bytes=" << args.bytes << "\n";
  return true;
}

bool RunRegisterProbe(const ProbeArgs &args, Hixl &hixl_engine, Allocation &alloc) {
  if (!Allocate(args.device_id, args.mode, args.bytes, alloc)) {
    std::cout << "[RESULT] mode=" << args.mode << " allocate=failed register=not_run\n";
    return false;
  }
  std::cout << "[INFO] Allocate success mode=" << args.mode << " addr=" << alloc.ptr << "\n";
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(alloc.ptr);
  desc.len = args.bytes;
  MemHandle handle = nullptr;
  auto ret = hixl_engine.RegisterMem(desc, MEM_DEVICE, handle);
  if (ret != SUCCESS) {
    std::cout << "[RESULT] mode=" << args.mode << " allocate=success register=failed ret=" << ret
              << " errmsg=" << RecentErrMsg() << "\n";
    return false;
  }
  std::cout << "[RESULT] mode=" << args.mode << " allocate=success register=success handle=" << handle << "\n";
  (void)hixl_engine.DeregisterMem(handle);
  return true;
}
}  // namespace

int main(int argc, char **argv) {
  ProbeArgs args{};
  if (!ParseArgs(argc, argv, args)) {
    return 2;
  }
  Hixl hixl_engine;
  if (!InitRuntime(args, hixl_engine)) {
    return 1;
  }
  Allocation alloc;
  const bool ok = RunRegisterProbe(args, hixl_engine, alloc);
  Cleanup(hixl_engine, alloc, args.device_id);
  return ok ? 0 : 1;
}
