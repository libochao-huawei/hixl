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
#include <fstream>

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

/**
 * @brief 解析 EID 的第6字节
 * @param eid 32字符的 EID 字符串
 * @return EidByte6Info 解析结果
 *
 * 第6字节格式：
 * - 高4位：die_id 和是否串口组
 *   - >= 4 (即 bit3=1) → die_id = 1
 *   - < 4 (即 bit3=0) → die_id = 0
 *   - == 3 或 7 → 串口组 EID (PG EID)
 * - 低4位：port 值 (0-15)
 *   - 0-8：物理串口 port
 *   - > 8：PG EID (非物理串口)
 */
EidByte6Info ParseEidByte6(const std::string& eid) {
    EidByte6Info info = {0};

    if (eid.length() < 12) {
        return info;
    }

    // 第6字节是 raw[5]，对应 EID 字符串中位置 10-11 (0-indexed)
    // 或者说是倒数第6和第5个字节
    std::string byte_str = eid.substr(10, 2);
    info.byte6 = std::stoi(byte_str, nullptr, 16);
    info.high_nibble = (info.byte6 >> 4) & 0xF;
    info.low_nibble = info.byte6 & 0xF;

    // 判断 die_id：高4位 bit3
    info.die_id = (info.high_nibble & 0x8) ? 1 : 0;

    // 判断是否 PG EID：高4位为 3 或 7
    info.is_pg_eid = (info.high_nibble == 0x3 || info.high_nibble == 0x7);

    // port 取低4位
    info.port = info.low_nibble;

    return info;
}

// ============ URMA Device 获取实现 ============

int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice>& urma_devices,
                          const std::string& json_path) {
    // 如果提供了 json_path，从文件加载
    if (!json_path.empty()) {
        std::cout << "[GetUrmaDeviceList] Loading from JSON file: " << json_path << std::endl;

        std::ifstream file(json_path);
        if (!file.is_open()) {
            std::cerr << "[GetUrmaDeviceList] Failed to open JSON file: " << json_path << std::endl;
            return ERROR_FILE_NOT_FOUND;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json_content = buffer.str();

        urma_devices.clear();

        // 简单的 JSON 解析：提取 "udmaX": [...] 格式
        size_t pos = 0;
        while ((pos = json_content.find("\"udma", pos)) != std::string::npos) {
            // 找到设备名称
            size_t name_start = pos + 1;
            size_t name_end = json_content.find("\"", name_start);
            if (name_end == std::string::npos) break;
            std::string dev_name = json_content.substr(name_start, name_end - name_start);

            // 找到 EID 数组
            size_t array_start = json_content.find("[", name_end);
            size_t array_end = json_content.find("]", array_start);
            if (array_start == std::string::npos || array_end == std::string::npos) {
                pos = name_end;
                continue;
            }
            std::string eid_array = json_content.substr(array_start + 1, array_end - array_start - 1);

            UrmaDevice urma_dev;
            urma_dev.name = dev_name;

            // 提取每个 EID
            size_t eid_pos = 0;
            while ((eid_pos = eid_array.find("\"", eid_pos)) != std::string::npos) {
                size_t eid_start = eid_pos + 1;
                size_t eid_end = eid_array.find("\"", eid_start);
                if (eid_end == std::string::npos) break;
                std::string eid = eid_array.substr(eid_start, eid_end - eid_start);
                if (!eid.empty()) {
                    urma_dev.eid_list.push_back(eid);
                }
                eid_pos = eid_end + 1;
            }

            if (!urma_dev.eid_list.empty()) {
                urma_devices.push_back(urma_dev);
                std::cout << "[GetUrmaDeviceList]   Loaded " << dev_name << " with " << urma_dev.eid_list.size() << " EIDs" << std::endl;
            }

            pos = array_end + 1;
        }

        if (urma_devices.empty()) {
            std::cerr << "[GetUrmaDeviceList] No devices found in JSON file" << std::endl;
            return ERROR_FILE_PARSE_FAILED;
        }

        std::cout << "[GetUrmaDeviceList] Loaded " << urma_devices.size() << " device(s) from JSON" << std::endl;
        return SUCCESS;
    }

    // 正常 DCMI 调用
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

int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo& rootinfo,
                         const std::string& json_path) {
    std::cout << "[BuildNpuRootInfo] npu_id=" << npu_id << ", is_server=" << is_server << std::endl;

    // 获取 URMA Device 列表
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

    // 确定 Mesh 层的 die_id
    int mesh_die_id = GetMeshDieId(npu_id, is_server);
    std::cout << "[BuildNpuRootInfo] Mesh die_id=" << mesh_die_id << std::endl;

    // 清空 rootinfo
    rootinfo.port_to_eid.clear();
    rootinfo.clos_pg_eids.clear();

    // 遍历 URMA Device 构建 rootinfo
    for (const auto& urma_dev : urma_devices) {
        if (urma_dev.eid_list.empty()) {
            continue;
        }

        // 遍历该 urma_device 下的所有 EID
        for (const auto& eid : urma_dev.eid_list) {
            EidByte6Info info = ParseEidByte6(eid);

            // 调试打印
            std::cout << "[BuildNpuRootInfo]   EID: " << eid
                      << ", byte6=0x" << std::hex << info.byte6 << std::dec
                      << ", high=0x" << std::hex << info.high_nibble << std::dec
                      << ", low=0x" << std::hex << info.low_nibble << std::dec
                      << ", die_id=" << info.die_id
                      << ", is_pg=" << (info.is_pg_eid ? "true" : "false")
                      << ", port=" << info.port << std::endl;

            // 处理 PG EID (串口组 EID)
            if (info.is_pg_eid) {
                // 收集所有 PG EID，并根据 die_id 区分
                // plane_pg_0: 与 Mesh 相反 die 上的 PG EID
                // plane_pg_1: 与 Mesh 相同 die 上的 PG EID
                ClosPgEidInfo pg_info;
                pg_info.eid = eid;
                pg_info.die_id = info.die_id;
                rootinfo.clos_pg_eids.push_back(pg_info);
                std::cout << "[BuildNpuRootInfo]   Add PG EID: " << eid << " (die_id=" << info.die_id << ")" << std::endl;
                continue;
            }

            // 处理物理串口 EID（只记录 Mesh 层的）
            if (info.port >= 0 && info.port <= 8 && info.die_id == mesh_die_id) {
                // Mesh 层串口: port 值直接使用
                std::string port_key = std::to_string(info.die_id) + "/" + std::to_string(info.port);
                rootinfo.port_to_eid[port_key] = eid;
                std::cout << "[BuildNpuRootInfo]   Add Mesh port: " << port_key << std::endl;
            }
            // CLOS 层的物理串口不需要记录，port > 8 的也不是物理串口
        }
    }

    std::cout << "[BuildNpuRootInfo] Built " << rootinfo.port_to_eid.size()
              << " port->eid mappings, clos_pg_eids=" << rootinfo.clos_pg_eids.size() << std::endl;
    for (const auto& kv : rootinfo.port_to_eid) {
        std::cout << "[BuildNpuRootInfo]   " << kv.first << " -> " << kv.second << std::endl;
    }
    for (const auto& pg : rootinfo.clos_pg_eids) {
        std::cout << "[BuildNpuRootInfo]   CLOS PG EID: " << pg.eid << std::endl;
    }

    return SUCCESS;
}

}  // namespace hixl
