/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file rootinfo_builder.cc
 * @brief RootInfo 构建模块实现
 *
 * 实现根据 NPU ID 构建 RootInfo 的功能
 */

#include "rootinfo_builder.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <dlfcn.h>
#include <unistd.h>
#include "common/hixl_log.h"

namespace hixl {

// ============ DCMI 接口函数指针（共享） ============
dcmi_init_func g_dcmi_init = nullptr;
dcmi_get_urma_device_cnt_func g_dcmi_get_urma_device_cnt = nullptr;
dcmi_get_eid_list_func g_dcmi_get_eid_list = nullptr;
dcmi_get_mainboard_id_func g_dcmi_get_mainboard_id = nullptr;
dcmi_get_logicid_from_phyid_func g_dcmi_get_logicid_from_phyid = nullptr;
dcmi_get_device_info_func g_dcmi_get_device_info = nullptr;

namespace {
void *g_dcmi_handle = nullptr;
volatile bool g_dcmi_loaded = false;
volatile int g_dcmi_init_status = -1;
}  // anonymous namespace

// ============ DCMI 接口动态加载 ============

int TryLoadDcmiSymbols() {
  g_dcmi_handle = dlopen("libdcmi.so", RTLD_LAZY);
  if (g_dcmi_handle == nullptr) {
    HIXL_LOGE(FAILED, "Failed to dlopen libdcmi.so: %s", dlerror());
    return -1;
  }

  g_dcmi_init = reinterpret_cast<dcmi_init_func>(dlsym(g_dcmi_handle, "dcmiv2_init"));
  g_dcmi_get_urma_device_cnt =
      reinterpret_cast<dcmi_get_urma_device_cnt_func>(dlsym(g_dcmi_handle, "dcmiv2_get_urma_device_cnt"));
  g_dcmi_get_eid_list =
      reinterpret_cast<dcmi_get_eid_list_func>(dlsym(g_dcmi_handle, "dcmiv2_get_eid_list_by_urma_dev_index"));
  g_dcmi_get_mainboard_id =
      reinterpret_cast<dcmi_get_mainboard_id_func>(dlsym(g_dcmi_handle, "dcmiv2_get_mainboard_id"));
  g_dcmi_get_logicid_from_phyid =
      reinterpret_cast<dcmi_get_logicid_from_phyid_func>(dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_by_chip_phy_id"));

  if (g_dcmi_get_logicid_from_phyid == nullptr) {
    g_dcmi_get_logicid_from_phyid =
        reinterpret_cast<dcmi_get_logicid_from_phyid_func>(dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_from_chip_phyid"));
  }

  g_dcmi_get_device_info = reinterpret_cast<dcmi_get_device_info_func>(dlsym(g_dcmi_handle, "dcmiv2_get_device_info"));

  if (g_dcmi_init == nullptr || g_dcmi_get_urma_device_cnt == nullptr || g_dcmi_get_eid_list == nullptr ||
      g_dcmi_get_mainboard_id == nullptr || g_dcmi_get_logicid_from_phyid == nullptr ||
      g_dcmi_get_device_info == nullptr) {
    HIXL_LOGE(FAILED, "Failed to load DCMI function symbols");
    dlclose(g_dcmi_handle);
    g_dcmi_handle = nullptr;
    return -1;
  }

  return 0;
}

int InitDcmiWithRetry() {
  const int max_wait_time = 10;

  for (int i = 0; i < max_wait_time; ++i) {
    g_dcmi_init_status = g_dcmi_init();
    if (g_dcmi_init_status == 0) {
      break;
    }
    sleep(1);
  }

  if (g_dcmi_init_status != 0) {
    HIXL_LOGE(FAILED, "DCMI init failed after %d retries", max_wait_time);
    dlclose(g_dcmi_handle);
    g_dcmi_handle = nullptr;
    g_dcmi_init_status = -1;
    return g_dcmi_init_status;
  }

  return 0;
}

int LoadDcmi() {
  if (g_dcmi_loaded) {
    return g_dcmi_init_status;
  }

  if (TryLoadDcmiSymbols() != 0) {
    g_dcmi_init_status = -1;
    g_dcmi_loaded = true;
    return g_dcmi_init_status;
  }

  if (InitDcmiWithRetry() != 0) {
    g_dcmi_init_status = -1;
    g_dcmi_loaded = true;
    return g_dcmi_init_status;
  }

  g_dcmi_loaded = true;
  return g_dcmi_init_status;
}

// ============ EID 解析实现 ============

EidByte6Info ParseEidByte6(const std::string &eid) {
  EidByte6Info info{};

  if (eid.length() < 12) {
    return info;
  }

  std::string byte_str = eid.substr(10, 2);
  info.byte6 = std::stoi(byte_str, nullptr, 16);
  info.high_nibble = (info.byte6 >> 4) & 0xF;
  info.low_nibble = info.byte6 & 0xF;
  info.die_id = (info.high_nibble & 0x4) ? 1 : 0;
  info.is_pg_eid = (info.high_nibble == 0x3 || info.high_nibble == 0x7);
  info.port = info.low_nibble;

  return info;
}

// ============ URMA Device 获取实现（内部函数） ============

namespace {

std::string ConvertEidToString(const unsigned char *raw);

int32_t LoadUrmaDevicesFromDcmi(int32_t npu_id, std::vector<UrmaDevice> &urma_devices) {
  if (LoadDcmi() != 0) {
    HIXL_LOGE(FAILED, "DCMI not loaded");
    return FAILED;
  }

  unsigned int logic_id = 0;
  if (g_dcmi_get_logicid_from_phyid(npu_id, &logic_id) != 0) {
    HIXL_LOGE(FAILED, "Failed to get logic id from npu id: %d", npu_id);
    return FAILED;
  }

  unsigned int dev_cnt = 0;
  int ret = g_dcmi_get_urma_device_cnt(logic_id, &dev_cnt);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Failed to get urma device count, ret=%d", ret);
    return FAILED;
  }

  for (size_t i = 0; i < dev_cnt; ++i) {
    UrmaDevice urma_dev;
    urma_dev.name = "udma" + std::to_string(i);

    dcmi_urma_eid_info_t eid_buf[MAX_EID_PER_UE];
    int eid_cnt = MAX_EID_PER_UE;
    ret = g_dcmi_get_eid_list(logic_id, i, eid_buf, &eid_cnt);
    if (ret != 0) {
      continue;
    }

    for (int j = 0; j < eid_cnt; ++j) {
      std::string eid_str = ConvertEidToString(eid_buf[j].eid.raw);
      if (!eid_str.empty() && eid_str != "00000000000000000000000000000000") {
        urma_dev.eid_list.push_back(eid_str);
      }
    }

    if (!urma_dev.eid_list.empty()) {
      urma_devices.push_back(urma_dev);
    }
  }

  return SUCCESS;
}

std::string ConvertEidToString(const unsigned char *raw) {
  std::ostringstream oss;
  for (int k = 0; k < 16; ++k) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(raw[k]);
  }
  return oss.str();
}

// ============ RootInfo 构建实现 ============

}  // anonymous namespace

int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice> &urma_devices) {
  urma_devices.clear();
  return LoadUrmaDevicesFromDcmi(npu_id, urma_devices);
}

/**
 * @brief 确定 Mesh 层的 die_id
 * @param npu_id NPU ID
 * @param is_server 是否为 Server 产品形态
 * @return Mesh 层所在的 die_id
 *
 * Server: Mesh 在 1die
 * Pod: 根据 npu_id % 8 判断，0-3 在 0die，4-7 在 1die
 */
int GetMeshDieId(int32_t npu_id, bool is_server) {
  if (is_server) {
    // Server: Mesh 在 1die
    return 1;
  } else {
    // Pod: Mesh 在哪个 die 取决于 npu_id % 8
    int mod = npu_id % 8;
    if (mod >= 0 && mod <= 3) {
      return 0;  // 前4个 NPU，Mesh 在 0die
    } else {
      return 1;  // 后4个 NPU，Mesh 在 1die
    }
  }
}

void CollectMeshPorts(const std::vector<UrmaDevice> &urma_devices, int mesh_die_id, NpuRootInfo &rootinfo);
void CollectClosPgEids(const std::vector<UrmaDevice> &urma_devices, int mesh_die_id, NpuRootInfo &rootinfo);
void PrintEidDebugInfo(const std::string &eid, const EidByte6Info &info);
void PrintRootInfo(const NpuRootInfo &rootinfo);

int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo &rootinfo) {
  HIXL_LOGI("npu_id=%d, is_server=%d", npu_id, is_server);

  std::vector<UrmaDevice> urma_devices;
  int32_t ret = GetUrmaDeviceList(npu_id, urma_devices);
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED, "Failed to get urma devices, ret=%d", ret);
    return ret;
  }

  if (urma_devices.empty()) {
    HIXL_LOGE(FAILED, "No urma devices for npu_id=%d", npu_id);
    return FAILED;
  }

  HIXL_LOGI("Got %zu urma device(s)", urma_devices.size());

  int mesh_die_id = GetMeshDieId(npu_id, is_server);
  HIXL_LOGI("Mesh die_id=%d", mesh_die_id);

  rootinfo.port_to_eid.clear();
  rootinfo.clos_pg_eids.clear();

  CollectMeshPorts(urma_devices, mesh_die_id, rootinfo);
  CollectClosPgEids(urma_devices, mesh_die_id, rootinfo);
  PrintRootInfo(rootinfo);

  return SUCCESS;
}

void CollectMeshPorts(const std::vector<UrmaDevice> &urma_devices, int mesh_die_id, NpuRootInfo &rootinfo) {
  for (const auto &urma_dev : urma_devices) {
    if (urma_dev.eid_list.empty()) {
      continue;
    }

    for (const auto &eid : urma_dev.eid_list) {
      EidByte6Info info = ParseEidByte6(eid);
      PrintEidDebugInfo(eid, info);

      if (info.is_pg_eid) {
        continue;
      }

      if (info.port >= 0 && info.port <= 8 && info.die_id == mesh_die_id) {
        std::string port_key = std::to_string(info.die_id) + "/" + std::to_string(info.port);
        rootinfo.port_to_eid[port_key] = eid;
        HIXL_LOGI("Add Mesh port: %s", port_key.c_str());
      }
    }
  }
}

void CollectClosPgEids(const std::vector<UrmaDevice> &urma_devices, int mesh_die_id, NpuRootInfo &rootinfo) {
  struct UrmaGroupInfo {
    std::string pg_eid;
    int die_id;
    size_t total_eids;
  };
  std::vector<UrmaGroupInfo> mesh_groups;
  std::vector<UrmaGroupInfo> non_mesh_groups;

  for (const auto &urma_dev : urma_devices) {
    if (urma_dev.eid_list.empty()) {
      continue;
    }

    std::string pg_eid;
    int pg_die_id = -1;
    for (const auto &eid : urma_dev.eid_list) {
      EidByte6Info info = ParseEidByte6(eid);
      if (info.is_pg_eid) {
        pg_eid = eid;
        pg_die_id = info.die_id;
      }
    }
    if (pg_eid.empty()) {
      continue;
    }

    size_t total_eids = urma_dev.eid_list.size();
    // mesh 组（7 直连串口 + 1 PG = 8 EID）跳过
    if (pg_die_id == mesh_die_id && total_eids == 8) {
      continue;
    }

    if (pg_die_id == mesh_die_id) {
      mesh_groups.push_back({pg_eid, pg_die_id, total_eids});
    } else {
      non_mesh_groups.push_back({pg_eid, pg_die_id, total_eids});
    }
  }

  // 非 mesh_die_id：取 EID 数最多的组的 PG 作为 plane_pg_0
  if (!non_mesh_groups.empty()) {
    auto best =
        std::max_element(non_mesh_groups.begin(), non_mesh_groups.end(),
                         [](const UrmaGroupInfo &a, const UrmaGroupInfo &b) { return a.total_eids < b.total_eids; });
    rootinfo.clos_pg_eids.push_back({best->pg_eid, best->die_id});
  }

  // mesh_die_id：多个 PG 取 EID 数第二多的作为 plane_pg_1
  if (mesh_groups.size() >= 2) {
    std::sort(mesh_groups.begin(), mesh_groups.end(),
              [](const UrmaGroupInfo &a, const UrmaGroupInfo &b) { return a.total_eids > b.total_eids; });
    rootinfo.clos_pg_eids.push_back({mesh_groups[1].pg_eid, mesh_groups[1].die_id});
  }
}

void PrintEidDebugInfo(const std::string &eid, const EidByte6Info &info) {
  HIXL_LOGD("EID: %s, byte6=0x%x, high=0x%x, low=0x%x, die_id=%d, is_pg=%s, port=%d", eid.c_str(), info.byte6,
            info.high_nibble, info.low_nibble, info.die_id, info.is_pg_eid ? "true" : "false", info.port);
}

void PrintRootInfo(const NpuRootInfo &rootinfo) {
  HIXL_LOGI("Built %zu port->eid mappings, clos_pg_eids=%zu", rootinfo.port_to_eid.size(),
            rootinfo.clos_pg_eids.size());
  for (const auto &kv : rootinfo.port_to_eid) {
    HIXL_LOGD("  %s -> %s", kv.first.c_str(), kv.second.c_str());
  }
  for (const auto &pg : rootinfo.clos_pg_eids) {
    HIXL_LOGD("  CLOS PG EID: %s", pg.eid.c_str());
  }
}

}  // namespace hixl
