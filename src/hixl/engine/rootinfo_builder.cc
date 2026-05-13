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
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <fstream>

namespace hixl {

// ============ DCMI 接口函数指针（共享） ============
dcmi_init_func g_dcmi_init = nullptr;
dcmi_get_urma_device_cnt_func g_dcmi_get_urma_device_cnt = nullptr;
dcmi_get_eid_list_func g_dcmi_get_eid_list = nullptr;
dcmi_get_mainboard_id_func g_dcmi_get_mainboard_id = nullptr;
dcmi_get_logicid_from_phyid_func g_dcmi_get_logicid_from_phyid = nullptr;
dcmi_get_device_info_func g_dcmi_get_device_info = nullptr;

namespace {
void* g_dcmi_handle = nullptr;
volatile bool g_dcmi_loaded = false;
volatile int g_dcmi_init_status = -1;
}  // anonymous namespace

// ============ DCMI 接口动态加载 ============

int TryLoadDcmiSymbols() {
    g_dcmi_handle = dlopen("libdcmi.so", RTLD_LAZY);
    if (g_dcmi_handle == nullptr) {
        std::cerr << "[TryLoadDcmiSymbols] Failed to dlopen libdcmi.so: " << dlerror() << std::endl;
        return -1;
    }

    g_dcmi_init = reinterpret_cast<dcmi_init_func>(dlsym(g_dcmi_handle, "dcmiv2_init"));
    g_dcmi_get_urma_device_cnt = reinterpret_cast<dcmi_get_urma_device_cnt_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_urma_device_cnt"));
    g_dcmi_get_eid_list = reinterpret_cast<dcmi_get_eid_list_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_eid_list_by_urma_dev_index"));
    g_dcmi_get_mainboard_id = reinterpret_cast<dcmi_get_mainboard_id_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_mainboard_id"));
    g_dcmi_get_logicid_from_phyid = reinterpret_cast<dcmi_get_logicid_from_phyid_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_by_chip_phy_id"));

    if (g_dcmi_get_logicid_from_phyid == nullptr) {
        g_dcmi_get_logicid_from_phyid = reinterpret_cast<dcmi_get_logicid_from_phyid_func>(
            dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_from_chip_phyid"));
    }

    g_dcmi_get_device_info = reinterpret_cast<dcmi_get_device_info_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_device_info"));

    if (g_dcmi_init == nullptr ||
        g_dcmi_get_urma_device_cnt == nullptr ||
        g_dcmi_get_eid_list == nullptr ||
        g_dcmi_get_mainboard_id == nullptr ||
        g_dcmi_get_logicid_from_phyid == nullptr ||
        g_dcmi_get_device_info == nullptr) {
        std::cerr << "[TryLoadDcmiSymbols] Failed to load DCMI function symbols" << std::endl;
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
        std::cerr << "[InitDcmiWithRetry] DCMI init failed after " << max_wait_time << " retries" << std::endl;
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

EidByte6Info ParseEidByte6(const std::string& eid) {
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

// 前向声明（函数之间存在调用依赖，需要声明在前）
int32_t ParseUrmaDevicesFromJsonSection(const std::string& npu_section,
                                         std::vector<UrmaDevice>& urma_devices);
void ExtractEidsFromArray(const std::string& eid_array, std::vector<std::string>& eid_list);
std::string ConvertEidToString(const unsigned char* raw);

int32_t LoadUrmaDevicesFromJson(int32_t npu_id, const std::string& json_path,
                                 std::vector<UrmaDevice>& urma_devices) {
    std::cout << "[LoadUrmaDevicesFromJson] Loading from JSON file: " << json_path << std::endl;

    std::ifstream file(json_path);
    if (!file.is_open()) {
        std::cerr << "[LoadUrmaDevicesFromJson] Failed to open JSON file: " << json_path << std::endl;
        return ERROR_FILE_NOT_FOUND;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_content = buffer.str();

    std::string npu_name = "npu" + std::to_string(npu_id);
    size_t npu_pos = json_content.find("\"" + npu_name + "\"");
    if (npu_pos == std::string::npos) {
        std::cerr << "[LoadUrmaDevicesFromJson] npu section not found: " << npu_name << std::endl;
        return ERROR_FILE_PARSE_FAILED;
    }

    size_t npu_start = json_content.find(":", npu_pos);
    if (npu_start == std::string::npos) {
        return ERROR_FILE_PARSE_FAILED;
    }
    npu_start = json_content.find("{", npu_start);
    if (npu_start == std::string::npos) {
        return ERROR_FILE_PARSE_FAILED;
    }

    size_t npu_end = npu_start + 1;
    int brace_count = 1;
    while (brace_count > 0 && npu_end < json_content.size()) {
        if (json_content[npu_end] == '{') brace_count++;
        else if (json_content[npu_end] == '}') brace_count--;
        npu_end++;
    }

    std::string npu_section = json_content.substr(npu_start + 1, npu_end - npu_start - 2);
    return ParseUrmaDevicesFromJsonSection(npu_section, urma_devices);
}

int32_t ParseUrmaDevicesFromJsonSection(const std::string& npu_section,
                                         std::vector<UrmaDevice>& urma_devices) {
    size_t pos = 0;
    while ((pos = npu_section.find("\"udma", pos)) != std::string::npos) {
        size_t name_start = pos + 1;
        size_t name_end = npu_section.find("\"", name_start);
        if (name_end == std::string::npos) break;
        std::string dev_name = npu_section.substr(name_start, name_end - name_start);

        size_t array_start = npu_section.find("[", name_end);
        size_t array_end = npu_section.find("]", array_start);
        if (array_start == std::string::npos || array_end == std::string::npos) {
            pos = name_end;
            continue;
        }
        std::string eid_array = npu_section.substr(array_start + 1, array_end - array_start - 1);

        UrmaDevice urma_dev;
        urma_dev.name = dev_name;
        ExtractEidsFromArray(eid_array, urma_dev.eid_list);

        if (!urma_dev.eid_list.empty()) {
            urma_devices.push_back(urma_dev);
            std::cout << "[ParseUrmaDevicesFromJsonSection]   Loaded " << dev_name
                      << " with " << urma_dev.eid_list.size() << " EIDs" << std::endl;
        }
        pos = array_end + 1;
    }

    if (urma_devices.empty()) {
        std::cerr << "[ParseUrmaDevicesFromJsonSection] No devices found in JSON" << std::endl;
        return ERROR_FILE_PARSE_FAILED;
    }

    std::cout << "[ParseUrmaDevicesFromJsonSection] Loaded " << urma_devices.size() << " device(s)" << std::endl;
    return SUCCESS;
}

void ExtractEidsFromArray(const std::string& eid_array, std::vector<std::string>& eid_list) {
    size_t eid_pos = 0;
    while ((eid_pos = eid_array.find("\"", eid_pos)) != std::string::npos) {
        size_t eid_start = eid_pos + 1;
        size_t eid_end = eid_array.find("\"", eid_start);
        if (eid_end == std::string::npos) break;
        std::string eid = eid_array.substr(eid_start, eid_end - eid_start);
        if (!eid.empty()) {
            eid_list.push_back(eid);
        }
        eid_pos = eid_end + 1;
    }
}

int32_t LoadUrmaDevicesFromDcmi(int32_t npu_id, std::vector<UrmaDevice>& urma_devices) {
    if (LoadDcmi() != 0) {
        std::cerr << "[LoadUrmaDevicesFromDcmi] DCMI not loaded" << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int logic_id = 0;
    if (g_dcmi_get_logicid_from_phyid(npu_id, &logic_id) != 0) {
        std::cerr << "[LoadUrmaDevicesFromDcmi] Failed to get logic id from npu id: " << npu_id << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int dev_cnt = 0;
    int ret = g_dcmi_get_urma_device_cnt(logic_id, &dev_cnt);
    if (ret != 0) {
        std::cerr << "[LoadUrmaDevicesFromDcmi] Failed to get urma device count, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
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

std::string ConvertEidToString(const unsigned char* raw) {
    std::ostringstream oss;
    for (int k = 0; k < 16; ++k) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(raw[k]);
    }
    return oss.str();
}

// ============ RootInfo 构建实现 ============

}  // anonymous namespace

int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice>& urma_devices,
                          const std::string& json_path) {
    urma_devices.clear();
    if (!json_path.empty()) {
        return LoadUrmaDevicesFromJson(npu_id, json_path, urma_devices);
    }
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

void CollectMeshPorts(const std::vector<UrmaDevice>& urma_devices,
                       int mesh_die_id,
                       NpuRootInfo& rootinfo);
void CollectClosPgEids(const std::vector<UrmaDevice>& urma_devices,
                        int mesh_die_id,
                        NpuRootInfo& rootinfo);
void PrintEidDebugInfo(const std::string& eid, const EidByte6Info& info);
void PrintRootInfo(const NpuRootInfo& rootinfo);

int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo& rootinfo,
                         const std::string& json_path) {
    std::cout << "[BuildNpuRootInfo] npu_id=" << npu_id << ", is_server=" << is_server << std::endl;

    std::vector<UrmaDevice> urma_devices;
    int32_t ret = GetUrmaDeviceList(npu_id, urma_devices, json_path);
    if (ret != SUCCESS) {
        std::cerr << "[BuildNpuRootInfo] Failed to get urma devices, ret=" << ret << std::endl;
        return ret;
    }

    if (urma_devices.empty()) {
        std::cerr << "[BuildNpuRootInfo] No urma devices for npu_id=" << npu_id << std::endl;
        return ERROR_NO_EID_FOUND;
    }

    std::cout << "[BuildNpuRootInfo] Got " << urma_devices.size() << " urma device(s)" << std::endl;

    int mesh_die_id = GetMeshDieId(npu_id, is_server);
    std::cout << "[BuildNpuRootInfo] Mesh die_id=" << mesh_die_id << std::endl;

    rootinfo.port_to_eid.clear();
    rootinfo.clos_pg_eids.clear();

    CollectMeshPorts(urma_devices, mesh_die_id, rootinfo);
    CollectClosPgEids(urma_devices, mesh_die_id, rootinfo);
    PrintRootInfo(rootinfo);

    return SUCCESS;
}

void CollectMeshPorts(const std::vector<UrmaDevice>& urma_devices,
                      int mesh_die_id,
                      NpuRootInfo& rootinfo) {
    for (const auto& urma_dev : urma_devices) {
        if (urma_dev.eid_list.empty()) continue;

        for (const auto& eid : urma_dev.eid_list) {
            EidByte6Info info = ParseEidByte6(eid);
            PrintEidDebugInfo(eid, info);

            if (info.is_pg_eid) continue;

            if (info.port >= 0 && info.port <= 8 && info.die_id == mesh_die_id) {
                std::string port_key = std::to_string(info.die_id) + "/" + std::to_string(info.port);
                rootinfo.port_to_eid[port_key] = eid;
                std::cout << "[CollectMeshPorts]   Add Mesh port: " << port_key << std::endl;
            }
        }
    }
}

void CollectClosPgEids(const std::vector<UrmaDevice>& urma_devices,
                        int mesh_die_id,
                        NpuRootInfo& rootinfo) {
    struct UrmaGroupInfo {
        std::string pg_eid;
        int die_id;
        size_t total_eids;
    };
    std::vector<UrmaGroupInfo> mesh_groups;
    std::vector<UrmaGroupInfo> non_mesh_groups;

    for (const auto& urma_dev : urma_devices) {
        if (urma_dev.eid_list.empty()) continue;

        std::string pg_eid;
        int pg_die_id = -1;
        for (const auto& eid : urma_dev.eid_list) {
            EidByte6Info info = ParseEidByte6(eid);
            if (info.is_pg_eid) {
                pg_eid = eid;
                pg_die_id = info.die_id;
            }
        }
        if (pg_eid.empty()) continue;

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
        auto best = std::max_element(non_mesh_groups.begin(), non_mesh_groups.end(),
            [](const UrmaGroupInfo& a, const UrmaGroupInfo& b) { return a.total_eids < b.total_eids; });
        rootinfo.clos_pg_eids.push_back({best->pg_eid, best->die_id});
    }

    // mesh_die_id：多个 PG 取 EID 数第二多的作为 plane_pg_1
    if (mesh_groups.size() >= 2) {
        std::sort(mesh_groups.begin(), mesh_groups.end(),
            [](const UrmaGroupInfo& a, const UrmaGroupInfo& b) { return a.total_eids > b.total_eids; });
        rootinfo.clos_pg_eids.push_back({mesh_groups[1].pg_eid, mesh_groups[1].die_id});
    }
}

void PrintEidDebugInfo(const std::string& eid, const EidByte6Info& info) {
    std::cout << "[BuildNpuRootInfo]   EID: " << eid
              << ", byte6=0x" << std::hex << info.byte6 << std::dec
              << ", high=0x" << std::hex << info.high_nibble << std::dec
              << ", low=0x" << std::hex << info.low_nibble << std::dec
              << ", die_id=" << info.die_id
              << ", is_pg=" << (info.is_pg_eid ? "true" : "false")
              << ", port=" << info.port << std::endl;
}

void PrintRootInfo(const NpuRootInfo& rootinfo) {
    std::cout << "[BuildNpuRootInfo] Built " << rootinfo.port_to_eid.size()
              << " port->eid mappings, clos_pg_eids=" << rootinfo.clos_pg_eids.size() << std::endl;
    for (const auto& kv : rootinfo.port_to_eid) {
        std::cout << "[BuildNpuRootInfo]   " << kv.first << " -> " << kv.second << std::endl;
    }
    for (const auto& pg : rootinfo.clos_pg_eids) {
        std::cout << "[BuildNpuRootInfo]   CLOS PG EID: " << pg.eid << std::endl;
    }
}

}  // namespace hixl
