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
#include <dlfcn.h>
#include <unistd.h>
#include <cstring>

namespace hixl {

// ============ DCMI 接口函数指针类型 ============

namespace {

typedef int (*dcmi_init_func)();
typedef int (*dcmi_get_urma_device_cnt_func)(int npu_id, unsigned int* dev_cnt);
typedef int (*dcmi_get_eid_list_func)(int npu_id, int urma_dev_index,
                                       dcmi_urma_eid_info_t* eid_list, int* eid_cnt);
typedef int (*dcmi_get_mainboard_id_func)(int npu_id, unsigned int* mainboard_id);
typedef int (*dcmi_get_logicid_from_phyid_func)(unsigned int phy_id, unsigned int* logic_id);

// DCMI 接口函数指针（全局）
dcmi_init_func g_dcmi_init = nullptr;
dcmi_get_urma_device_cnt_func g_dcmi_get_urma_device_cnt = nullptr;
dcmi_get_eid_list_func g_dcmi_get_eid_list = nullptr;
dcmi_get_mainboard_id_func g_dcmi_get_mainboard_id = nullptr;
dcmi_get_logicid_from_phyid_func g_dcmi_get_logicid_from_phyid = nullptr;

// DCMI 库句柄
void* g_dcmi_handle = nullptr;

// 加载状态
volatile bool g_dcmi_loaded = false;
volatile int g_dcmi_init_status = -1;

// ============ DCMI 接口动态加载 ============

int LoadDcmi() {
    if (g_dcmi_loaded) {
        return g_dcmi_init_status;
    }

    const int max_wait_time = 10;

    // 打开 DCMI 库
    g_dcmi_handle = dlopen("libdcmi.so", RTLD_LAZY);
    if (g_dcmi_handle == nullptr) {
        std::cerr << "[LoadDcmi] Failed to dlopen libdcmi.so: " << dlerror() << std::endl;
        g_dcmi_init_status = -1;
        g_dcmi_loaded = true;
        return g_dcmi_init_status;
    }

    // 加载函数符号
    g_dcmi_init = reinterpret_cast<dcmi_init_func>(dlsym(g_dcmi_handle, "dcmiv2_init"));
    g_dcmi_get_urma_device_cnt = reinterpret_cast<dcmi_get_urma_device_cnt_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_urma_device_cnt"));
    g_dcmi_get_eid_list = reinterpret_cast<dcmi_get_eid_list_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_eid_list_by_urma_dev_index"));
    g_dcmi_get_mainboard_id = reinterpret_cast<dcmi_get_mainboard_id_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_mainboard_id"));
    g_dcmi_get_logicid_from_phyid = reinterpret_cast<dcmi_get_logicid_from_phyid_func>(
        dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_by_chip_phy_id"));

    // 尝试备用符号名
    if (g_dcmi_get_logicid_from_phyid == nullptr) {
        g_dcmi_get_logicid_from_phyid = reinterpret_cast<dcmi_get_logicid_from_phyid_func>(
            dlsym(g_dcmi_handle, "dcmiv2_get_dev_id_from_chip_phyid"));
    }

    // 检查必要函数
    if (g_dcmi_init == nullptr ||
        g_dcmi_get_urma_device_cnt == nullptr ||
        g_dcmi_get_eid_list == nullptr ||
        g_dcmi_get_mainboard_id == nullptr ||
        g_dcmi_get_logicid_from_phyid == nullptr) {
        std::cerr << "[LoadDcmi] Failed to load DCMI function symbols" << std::endl;
        dlclose(g_dcmi_handle);
        g_dcmi_handle = nullptr;
        g_dcmi_init_status = -1;
        g_dcmi_loaded = true;
        return g_dcmi_init_status;
    }

    // 初始化 DCMI
    for (int i = 0; i < max_wait_time; ++i) {
        g_dcmi_init_status = g_dcmi_init();
        if (g_dcmi_init_status == 0) {
            break;
        }
        sleep(1);
    }

    if (g_dcmi_init_status != 0) {
        std::cerr << "[LoadDcmi] DCMI init failed after " << max_wait_time << " retries" << std::endl;
        dlclose(g_dcmi_handle);
        g_dcmi_handle = nullptr;
        g_dcmi_init_status = -1;
        g_dcmi_loaded = true;
        return g_dcmi_init_status;
    }

    g_dcmi_loaded = true;
    return g_dcmi_init_status;
}

}  // anonymous namespace

// ============ EID 解析实现 ============

int GetPortFromEid(const std::string& eid) {
    if (eid.length() < 2) {
        return -1;
    }
    std::string last = eid.substr(eid.length() - 2);
    int h = std::stoi(last, nullptr, 16);
    int p = ((~128) & h) >> 3;
    return p;
}

int GetServerDieIdFromEid(const std::string& eid) {
    if (eid.length() < 2) {
        return -1;
    }
    char low = eid[eid.length() - 2];
    int h = std::stoi(std::string(1, low), nullptr, 16);
    int die_id = (8 & h) >> 3;
    return die_id;
}

int GetPodDieIdFromEid(const std::string& eid) {
    if (eid.length() < 3) {
        return -1;
    }
    char third = eid[eid.length() - 3];
    int h = std::stoi(std::string(1, third), nullptr, 16);
    int die_id = (4 & h) >> 2;
    return die_id;
}

bool IsMeshLayerEid(const std::string& eid) {
    int port = GetPortFromEid(eid);
    return port >= 0 && port <= 9;
}

bool IsClosLayerEid(const std::string& eid) {
    int port = GetPortFromEid(eid);
    return port > 9;
}

// ============ URMA Device 获取实现 ============

int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice>& urma_devices) {
    if (LoadDcmi() != 0) {
        std::cerr << "[GetUrmaDeviceList] DCMI not loaded" << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int logic_id = 0;
    if (g_dcmi_get_logicid_from_phyid(npu_id, &logic_id) != 0) {
        std::cerr << "[GetUrmaDeviceList] Failed to get logic id from npu id: " << npu_id << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int dev_cnt = 0;
    int ret = g_dcmi_get_urma_device_cnt(logic_id, &dev_cnt);
    if (ret != 0) {
        std::cerr << "[GetUrmaDeviceList] Failed to get urma device count, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    urma_devices.clear();
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
            // 直接将 raw[16] 字节数组转换为十六进制字符串（已经是正确顺序）
            std::ostringstream oss;
            for (int k = 0; k < 16; ++k) {
                oss << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<int>(eid_buf[j].eid.raw[k]);
            }
            std::string eid_str = oss.str();

            // 跳过空 EID
            if (eid_str.empty() || eid_str == "00000000000000000000000000000000") {
                continue;
            }
            urma_dev.eid_list.push_back(eid_str);
        }

        // 跳过空设备（UBOE 设备在未配置 EID 之前是空的）
        if (urma_dev.eid_list.empty()) {
            continue;
        }

        urma_devices.push_back(urma_dev);
    }

    return SUCCESS;
}

// ============ RootInfo 构建实现 ============

int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo& rootinfo) {
    std::cout << "[BuildNpuRootInfo] npu_id=" << npu_id << ", is_server=" << is_server << std::endl;

    // 获取 URMA Device 列表
    std::vector<UrmaDevice> urma_devices;
    int32_t ret = GetUrmaDeviceList(npu_id, urma_devices);
    if (ret != SUCCESS) {
        std::cerr << "[BuildNpuRootInfo] Failed to get urma devices, ret=" << ret << std::endl;
        return ret;
    }

    if (urma_devices.empty()) {
        std::cerr << "[BuildNpuRootInfo] No urma devices for npu_id=" << npu_id << std::endl;
        return ERROR_NO_EID_FOUND;
    }

    std::cout << "[BuildNpuRootInfo] Got " << urma_devices.size() << " urma device(s)" << std::endl;

    // 清空 rootinfo
    rootinfo.port_to_eid.clear();
    rootinfo.clos_pg_eid.clear();

    // 遍历 URMA Device 构建 rootinfo
    for (const auto& urma_dev : urma_devices) {
        if (urma_dev.eid_list.empty()) {
            continue;
        }

        // 从第一个 EID 获取 die_id（参考 Python 的 urma_device.get_die_id()）
        const std::string& first_eid = urma_dev.eid_list[0];
        int die_id = is_server ? GetServerDieIdFromEid(first_eid) : GetPodDieIdFromEid(first_eid);

        // Python 逻辑: if die_id == 0: continue (跳过 die_id == 0 的 urma_device)
        if (die_id == 0) {
            std::cout << "[BuildNpuRootInfo] Skip urma_device (die_id == 0): " << urma_dev.name << std::endl;
            continue;
        }

        // 遍历该 urma_device 下的所有 EID
        for (const auto& eid : urma_dev.eid_list) {
            int port = GetPortFromEid(eid);
            if (port >= 1 && port <= 9) {
                // Mesh 层有效端口: port 1-9 映射到 0-8
                std::string port_key = std::to_string(die_id) + "/" + std::to_string(port - 1);
                rootinfo.port_to_eid[port_key] = eid;
            } else if (port > 9 && rootinfo.clos_pg_eid.empty()) {
                // CLOS 层 PG EID（串口组标识），只取第一个
                rootinfo.clos_pg_eid = eid;
            }
        }
    }

    std::cout << "[BuildNpuRootInfo] Built " << rootinfo.port_to_eid.size()
              << " port->eid mappings, clos_pg_eid="
              << (rootinfo.clos_pg_eid.empty() ? "(none)" : rootinfo.clos_pg_eid) << std::endl;
    for (const auto& kv : rootinfo.port_to_eid) {
        std::cout << "[BuildNpuRootInfo]   " << kv.first << " -> " << kv.second << std::endl;
    }

    return SUCCESS;
}

}  // namespace hixl
