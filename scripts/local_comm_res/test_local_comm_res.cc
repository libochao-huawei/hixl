/**
 * @file test_local_comm_res.cc
 * @brief LocalCommRes 生成工具测试用例
 *
 * 测试流程：
 * 1. 设置设备 (aclrtSetDevice)
 * 2. 获取设备信息 (aclrtGetDevice, aclrtGetPhyDevIdByLogicDevId)
 * 3. 调用 DCMI 接口获取 EID 列表
 * 4. 生成 LocalCommRes
 * 5. 将结果写入 JSON 文件
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "acl/acl.h"
#include "local_comm_res_tool.h"

namespace {

const char* TEST_OUTPUT_FILE = "./test_output.json";

// 将 LocalCommRes 结构体转换为 JSON 字符串
std::string LocalCommResToJson(const hixl::LocalCommRes& res) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"version\": \"" << res.version << "\",\n";

    if (res.net_instance_id.empty()) {
        oss << "  \"net_instance_id\": \"\",\n";
    } else {
        oss << "  \"net_instance_id\": \"" << res.net_instance_id << "\",\n";
    }

    oss << "  \"endpoint_list\": [\n";

    for (size_t i = 0; i < res.endpoint_list.size(); ++i) {
        const auto& ep = res.endpoint_list[i];
        oss << "    {\n";
        oss << "      \"protocol\": \"" << ep.protocol << "\",\n";
        oss << "      \"comm_id\": \"" << ep.comm_id << "\",\n";
        oss << "      \"placement\": \"" << ep.placement << "\"";

        if (!ep.plane.empty()) {
            oss << ",\n      \"plane\": \"" << ep.plane << "\"";
        }

        if (!ep.dst_eid.empty()) {
            oss << ",\n      \"dst_eid\": \"" << ep.dst_eid << "\"";
        }

        oss << "\n    }";

        if (i < res.endpoint_list.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }

    oss << "  ]\n";
    oss << "}\n";

    return oss.str();
}

// 在 /etc/ 目录下模糊匹配以 noroce.json 结尾的文件，返回修改时间最新的一个
std::string FindLatestTopoFile() {
    const char* dir_path = "/etc/";
    const char* suffix = "noroce.json";
    DIR* dir = opendir(dir_path);
    if (!dir) {
        std::cerr << "[Test] WARNING: Failed to open /etc/ for topo file scan" << std::endl;
        return "";
    }

    std::string latest_file;
    time_t latest_mtime = 0;
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.size() < std::strlen(suffix)) {
            continue;
        }
        if (name.compare(name.size() - std::strlen(suffix), std::strlen(suffix), suffix) != 0) {
            continue;
        }

        std::string full_path = std::string(dir_path) + name;
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            if (st.st_mtime > latest_mtime) {
                latest_mtime = st.st_mtime;
                latest_file = full_path;
            }
        }
    }
    closedir(dir);
    return latest_file;
}

// 打印 URMA Device 列表（用于调试）
void PrintUrmaDeviceList(const std::vector<hixl::UrmaDevice>& urma_devices) {
    std::cout << "[Test] URMA device count: " << urma_devices.size() << std::endl;
    for (size_t i = 0; i < urma_devices.size(); ++i) {
        const auto& dev = urma_devices[i];
        std::cout << "[Test]   Device[" << i << "]: " << dev.name
                  << ", eid_count=" << dev.eid_list.size() << std::endl;
        for (size_t j = 0; j < dev.eid_list.size(); ++j) {
            const std::string& eid = dev.eid_list[j];
            hixl::EidByte6Info info = hixl::ParseEidByte6(eid);
            std::cout << "[Test]     EID[" << j << "]: " << eid
                      << " (byte6=0x" << std::hex << info.byte6 << std::dec
                      << ", die_id=" << info.die_id
                      << ", is_pg=" << (info.is_pg_eid ? "true" : "false")
                      << ", port=" << info.port << ")" << std::endl;
        }
    }
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    std::cout << "============================================" << std::endl;
    std::cout << "  LocalCommRes 生成工具测试用例" << std::endl;
    std::cout << "============================================" << std::endl;

    // 1. 解析命令行参数
    int32_t deviceId = 0;  // 默认设备 ID
    std::string topoPath;
    std::string routePath = "/lib/route.conf";
    std::string eidJsonPath;  // EID JSON 文件路径

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            deviceId = std::atoi(argv[++i]);
        } else if (arg == "--topo" && i + 1 < argc) {
            topoPath = argv[++i];
        } else if (arg == "--route" && i + 1 < argc) {
            routePath = argv[++i];
        } else if (arg == "--eid-json" && i + 1 < argc) {
            eidJsonPath = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --device <id>   Set device ID (default: 0)" << std::endl;
            std::cout << "  --topo <path>   Set topology file path" << std::endl;
            std::cout << "  --route <path>  Set route.conf path" << std::endl;
            std::cout << "  --eid-json <path>  Set EID JSON file path (skip DCMI)" << std::endl;
            std::cout << "  --help          Show this help message" << std::endl;
            return 0;
        }
    }

    // 如果未通过 --topo 指定，则在 /etc/ 下模糊匹配最新的 *noroce.json
    if (topoPath.empty()) {
        std::string found = FindLatestTopoFile();
        if (!found.empty()) {
            topoPath = found;
        } else {
            topoPath = "/etc/superpod_2d_noroce.json";
            std::cerr << "[Test] WARNING: No *noroce.json found in /etc/, fallback to default" << std::endl;
        }
    }

    std::cout << "[Test] Configuration:" << std::endl;
    std::cout << "[Test]   device_id: " << deviceId << std::endl;
    std::cout << "[Test]   topo_path: " << topoPath << std::endl;
    std::cout << "[Test]   route_path: " << routePath << std::endl;
    std::cout << "[Test]   eidJsonPath: " << eidJsonPath << std::endl;
    std::cout << "[Test] ===== Start ACL Init =====" << std::endl;

    // 2. ACL 初始化
    std::cout << "[Test] [INFO] Calling aclInit..." << std::endl;
    aclError ret = aclInit(nullptr);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[Test] ERROR: aclInit failed with error " << ret << std::endl;
        return -1;
    }
    std::cout << "[Test] [INFO] aclInit succeeded" << std::endl;

    // 3. 设置设备
    std::cout << "[Test] [INFO] Calling aclrtSetDevice(" << deviceId << ")..." << std::endl;
    ret = aclrtSetDevice(deviceId);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[Test] ERROR: aclrtSetDevice failed with error " << ret << std::endl;
        aclFinalize();
        return -1;
    }
    std::cout << "[Test] [INFO] aclrtSetDevice(" << deviceId << ") succeeded" << std::endl;

    // 4. 获取设备信息
    std::cout << "[Test] [INFO] Calling aclrtGetDevice..." << std::endl;
    int32_t logicId = 0;
    int32_t phyId = 0;

    ret = aclrtGetDevice(&logicId);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[Test] ERROR: aclrtGetDevice failed with error " << ret << std::endl;
        aclrtResetDevice(deviceId);
        aclFinalize();
        return -1;
    }
    std::cout << "[Test] [INFO] aclrtGetDevice: logicId=" << logicId << std::endl;

    std::cout << "[Test] [INFO] Calling aclrtGetPhyDevIdByLogicDevId..." << std::endl;
    ret = aclrtGetPhyDevIdByLogicDevId(logicId, &phyId);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[Test] ERROR: aclrtGetPhyDevIdByLogicDevId failed with error " << ret << std::endl;
        aclrtResetDevice(deviceId);
        aclFinalize();
        return -1;
    }
    std::cout << "[Test] [INFO] aclrtGetPhyDevIdByLogicDevId: phyId=" << phyId << std::endl;

    // 5. 直接调用 DCMI 接口获取 URMA Device 列表（用于验证）
    std::cout << "\n[Test] ===== DCMI Interface Test =====" << std::endl;
    std::cout << "[Test] [INFO] Calling GetUrmaDeviceList..." << std::endl;
    std::vector<hixl::UrmaDevice> urmaDevices;
    int32_t dcmiRet = hixl::GetUrmaDeviceList(phyId, urmaDevices);
    if (dcmiRet != hixl::SUCCESS) {
        std::cerr << "[Test] ERROR: GetUrmaDeviceList failed with error " << dcmiRet << std::endl;
    } else {
        PrintUrmaDeviceList(urmaDevices);
    }

    // 6. 获取主板 ID（用于验证）
    unsigned int mainboardId = 0;
    std::cout << "[Test] [INFO] Calling GetMainboardId..." << std::endl;
    int32_t mbRet = hixl::GetMainboardId(phyId, mainboardId);
    if (mbRet != hixl::SUCCESS) {
        std::cerr << "[Test] WARNING: GetMainboardId failed with error " << mbRet << std::endl;
    } else {
        std::cout << "[Test] [INFO] Mainboard ID: 0x" << std::hex << mainboardId << std::dec << std::endl;
    }

    // 7. 生成 LocalCommRes
    std::cout << "\n[Test] ===== Generate LocalCommRes =====" << std::endl;

    std::map<std::string, std::string> options;
    options["topo_path"] = topoPath;
    options["route_path"] = routePath;
    if (!eidJsonPath.empty()) {
        options["eid_json_path"] = eidJsonPath;
    }

    hixl::LocalCommRes localCommRes;
    std::cout << "[Test] [INFO] Calling GenerateLocalCommRes..." << std::endl;
    int32_t genRet = hixl::GenerateLocalCommRes(phyId, options, localCommRes);

    if (genRet != hixl::SUCCESS) {
        std::cerr << "[Test] ERROR: GenerateLocalCommRes failed with error " << genRet << std::endl;
        aclrtResetDevice(deviceId);
        aclFinalize();
        return -1;
    }

    std::cout << "[Test] GenerateLocalCommRes succeeded" << std::endl;
    std::cout << "[Test]   version: " << localCommRes.version << std::endl;
    std::cout << "[Test]   endpoint_list size: " << localCommRes.endpoint_list.size() << std::endl;

    // 8. 打印生成的边信息
    std::cout << "\n[Test] ===== Generated Edges =====" << std::endl;
    for (size_t i = 0; i < localCommRes.endpoint_list.size(); ++i) {
        const auto& ep = localCommRes.endpoint_list[i];
        std::cout << "[Test] Edge[" << i << "]: protocol=" << ep.protocol
                  << ", placement=" << ep.placement
                  << ", comm_id=" << ep.comm_id;
        if (!ep.plane.empty()) {
            std::cout << ", plane=" << ep.plane;
        }
        if (!ep.dst_eid.empty()) {
            std::cout << ", dst_eid=" << ep.dst_eid;
        }
        std::cout << std::endl;
    }

    // 9. 写入 JSON 文件
    std::cout << "[Test] [INFO] Writing output to file..." << std::endl;
    std::string jsonContent = LocalCommResToJson(localCommRes);

    std::ofstream outFile(TEST_OUTPUT_FILE);
    if (!outFile.is_open()) {
        std::cerr << "[Test] ERROR: Failed to open output file: " << TEST_OUTPUT_FILE << std::endl;
        aclrtResetDevice(deviceId);
        aclFinalize();
        return -1;
    }

    outFile << jsonContent;
    outFile.close();

    std::cout << "\n[Test] ===== Write Output =====" << std::endl;
    std::cout << "[Test] Output written to: " << TEST_OUTPUT_FILE << std::endl;
    std::cout << "[Test] JSON content:" << std::endl;
    std::cout << jsonContent << std::endl;

    // 10. 清理资源
    aclrtResetDevice(deviceId);
    aclFinalize();

    std::cout << "\n[Test] ===== Test Complete =====" << std::endl;
    std::cout << "[Test] Result: SUCCESS" << std::endl;

    return 0;
}