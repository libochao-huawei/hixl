/**
 * @file local_comm_res_tool.cc
 * @brief LocalCommRes 生成工具实现文件
 *
 * 本文件负责：
 * - DCMI 接口封装（GetUBEntityList, GetMainboardId）
 * - 文件解析（ParseTopoFile, ParseRouteFile）
 * - 边生成（GenerateD2DEdges, GenerateD2UEdges, GenerateH2DEdges, GenerateH2UEdges）
 * - 核心流程（GenerateLocalCommRes）
 *
 * RootInfo 构建功能已拆分到 rootinfo_builder 模块
 */

#include "local_comm_res_tool.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <set>
#include <dlfcn.h>
#include <unistd.h>

namespace hixl {

// ============ DCMI 接口动态加载 ============

namespace {

// DCMI 接口函数指针类型
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

/**
 * @brief 动态加载 DCMI 接口
 */
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
    std::cout << "[LoadDcmi] DCMI loaded successfully" << std::endl;
    return g_dcmi_init_status;
}

/**
 * @brief 获取 logic ID 从 phy ID
 */
int GetLogicIdFromPhyId(unsigned int phy_id, unsigned int* logic_id) {
    if (LoadDcmi() != 0) {
        return -1;
    }
    return g_dcmi_get_logicid_from_phyid(phy_id, logic_id);
}

// ============ JSON 解析辅助函数 ============

bool ParseJsonFieldInt(const std::string& obj, const std::string& key, int32_t& value) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = obj.find(pattern);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos + pattern.length());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < obj.length() && std::isspace(static_cast<unsigned char>(obj[pos]))) ++pos;
    size_t end = pos;
    while (end < obj.length() && (std::isdigit(static_cast<unsigned char>(obj[end])) || obj[end] == '-')) ++end;
    if (end == pos) return false;
    value = std::stoi(obj.substr(pos, end - pos));
    return true;
}

bool ParseJsonFieldString(const std::string& obj, const std::string& key, std::string& value) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = obj.find(pattern);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos + pattern.length());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < obj.length() && std::isspace(static_cast<unsigned char>(obj[pos]))) ++pos;
    if (pos >= obj.length() || obj[pos] != '"') return false;
    ++pos;
    size_t end = obj.find('"', pos);
    if (end == std::string::npos) return false;
    value = obj.substr(pos, end - pos);
    return true;
}

bool ParseJsonFieldStringArray(const std::string& obj, const std::string& key, std::vector<std::string>& values) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = obj.find(pattern);
    if (pos == std::string::npos) return false;
    pos = obj.find('[', pos + pattern.length());
    if (pos == std::string::npos) return false;
    ++pos;
    values.clear();
    int bracket_depth = 1;
    std::string current_str;
    bool in_string = false;
    while (pos < obj.length() && bracket_depth > 0) {
        if (obj[pos] == '[') {
            ++bracket_depth;
        } else if (obj[pos] == ']') {
            --bracket_depth;
            if (bracket_depth == 0) break;
        } else if (obj[pos] == '"' && !in_string) {
            in_string = true;
        } else if (obj[pos] == '"' && in_string) {
            values.push_back(current_str);
            current_str.clear();
            in_string = false;
        } else if (in_string) {
            current_str += obj[pos];
        }
        ++pos;
    }
    return true;
}

std::vector<std::string> ExtractJsonObjects(const std::string& json, const std::string& array_key) {
    std::vector<std::string> objects;
    size_t array_pos = json.find("\"" + array_key + "\"");
    if (array_pos == std::string::npos) return objects;
    size_t bracket_pos = json.find('[', array_pos);
    if (bracket_pos == std::string::npos) return objects;
    size_t pos = bracket_pos + 1;
    int brace_depth = 0;
    size_t obj_start = std::string::npos;
    while (pos < json.length()) {
        if (json[pos] == '{') {
            if (brace_depth == 0) {
                obj_start = pos;
            }
            ++brace_depth;
        } else if (json[pos] == '}') {
            --brace_depth;
            if (brace_depth == 0 && obj_start != std::string::npos) {
                objects.push_back(json.substr(obj_start, pos - obj_start + 1));
                obj_start = std::string::npos;
            }
        }
        ++pos;
    }
    return objects;
}

}  // anonymous namespace

// ============ DCMI 接口封装实现 ============

int32_t GetUBEntityList(int32_t phy_dev_id, UEList& ue_list) {
    if (LoadDcmi() != 0) {
        std::cerr << "[GetUBEntityList] DCMI not loaded" << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    memset(&ue_list, 0, sizeof(UEList));

    unsigned int logic_id = 0;
    if (GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
        std::cerr << "[GetUBEntityList] Failed to get logic id from phy id: " << phy_dev_id << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    int ret = g_dcmi_get_urma_device_cnt(logic_id, &ue_list.ueNum);
    if (ret != 0) {
        std::cerr << "[GetUBEntityList] Failed to get urma device count, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    for (size_t i = 0; i < ue_list.ueNum && i < MAX_UE_PER_NPU; ++i) {
        int num = MAX_EID_PER_UE;
        ret = g_dcmi_get_eid_list(logic_id, i, ue_list.ueList[i].eidList, &num);
        if (ret != 0) {
            continue;
        }
        ue_list.ueList[i].eidNum = num;
    }

    return SUCCESS;
}

int32_t GetMainboardId(int32_t phy_dev_id, unsigned int& mainboard_id) {
    if (LoadDcmi() != 0) {
        std::cerr << "[GetMainboardId] DCMI not loaded" << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int logic_id = 0;
    if (GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
        std::cerr << "[GetMainboardId] Failed to get logic id from phy id: " << phy_dev_id << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    int ret = g_dcmi_get_mainboard_id(logic_id, &mainboard_id);
    if (ret != 0) {
        std::cerr << "[GetMainboardId] Failed to get mainboard id, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    return SUCCESS;
}

// ============ CLOS 层专用接口实现 ============

std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigServer(unsigned int mainboard_id) {
    std::vector<std::tuple<int, int, std::vector<int>>> config;

    const unsigned int MAIN_BOARD_ID_SERVER_TYPE1 = 0x23;
    const unsigned int MAIN_BOARD_ID_SERVER_8PMESH = 0x25;
    const unsigned int MAIN_BOARD_ID_SERVER_16PMESH = 0x44;

    switch (mainboard_id) {
        case MAIN_BOARD_ID_SERVER_TYPE1:
        case MAIN_BOARD_ID_SERVER_8PMESH:
        case MAIN_BOARD_ID_SERVER_16PMESH:
            if (mainboard_id == 0x23) {
                config.push_back(std::make_tuple(0, 3, std::vector<int>{4, 5, 6, 7}));
                config.push_back(std::make_tuple(1, 2, std::vector<int>{5, 6}));
            } else {
                config.push_back(std::make_tuple(0, 3, std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
            }
            break;
        default:
            config.push_back(std::make_tuple(0, 3, std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
            break;
    }
    return config;
}

std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigPod(unsigned int mainboard_id, int phy_id) {
    std::vector<std::tuple<int, int, std::vector<int>>> config;

    const unsigned int MAIN_BOARD_ID_POD_2D = 0x03;

    if (mainboard_id == MAIN_BOARD_ID_POD_2D) {
        if ((phy_id % 8) < 4) {
            config.push_back(std::make_tuple(0, 2, std::vector<int>{1, 2}));
            config.push_back(std::make_tuple(1, 2, std::vector<int>{0, 1, 2, 3, 5, 6}));
        } else {
            config.push_back(std::make_tuple(1, 2, std::vector<int>{1, 2}));
            config.push_back(std::make_tuple(0, 2, std::vector<int>{0, 1, 2, 3, 4, 5}));
        }
    }
    return config;
}

// ============ 文件解析实现 ============

int32_t ParseTopoFile(const std::string& topo_path, TopoData& topo_data) {
    topo_data.links.clear();
    std::ifstream file(topo_path);
    if (!file.is_open()) {
        std::cerr << "[ParseTopoFile] Failed to open file: " << topo_path << std::endl;
        return ERROR_FILE_NOT_FOUND;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    if (content.empty()) {
        std::cerr << "[ParseTopoFile] File is empty: " << topo_path << std::endl;
        return ERROR_FILE_PARSE_FAILED;
    }

    std::vector<std::string> edge_objects = ExtractJsonObjects(content, "edge_list");
    if (edge_objects.empty()) {
        std::cerr << "[ParseTopoFile] No edge_list found in: " << topo_path << std::endl;
        return ERROR_FILE_PARSE_FAILED;
    }

    for (const auto& obj : edge_objects) {
        TopoLink link;
        link.remote_a = -1;
        link.remote_b = -1;

        if (!ParseJsonFieldInt(obj, "net_layer", link.net_layer)) {
            std::cerr << "[ParseTopoFile] Missing net_layer in edge object" << std::endl;
            continue;
        }
        ParseJsonFieldString(obj, "link_type", link.link_type);
        ParseJsonFieldString(obj, "topo_type", link.topo_type);
        ParseJsonFieldInt(obj, "local_a", link.local_a);
        ParseJsonFieldInt(obj, "local_b", link.local_b);
        ParseJsonFieldInt(obj, "remote_a", link.remote_a);
        ParseJsonFieldInt(obj, "remote_b", link.remote_b);
        ParseJsonFieldStringArray(obj, "local_a_ports", link.local_a_ports);
        ParseJsonFieldStringArray(obj, "local_b_ports", link.local_b_ports);

        topo_data.links.push_back(link);
    }

    std::cout << "[ParseTopoFile] Parsed " << topo_data.links.size() << " links from " << topo_path << std::endl;
    return SUCCESS;
}

int32_t ParseRouteFile(const std::string& route_path, RouteData& route_data) {
    route_data.entries.clear();
    std::ifstream file(route_path);
    if (!file.is_open()) {
        std::cerr << "[ParseRouteFile] Failed to open file: " << route_path << std::endl;
        return ERROR_FILE_NOT_FOUND;
    }

    std::map<std::string, std::string> kv_map;
    std::string line;
    while (std::getline(file, line)) {
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);
            // trim trailing whitespace from key, leading/trailing from value
            size_t key_end = key.find_last_not_of(" \t\r\n");
            if (key_end != std::string::npos) key = key.substr(0, key_end + 1);
            size_t val_start = value.find_first_not_of(" \t\r\n");
            size_t val_end = value.find_last_not_of(" \t\r\n");
            if (val_start != std::string::npos && val_end != std::string::npos) {
                value = value.substr(val_start, val_end - val_start + 1);
            }
            kv_map[key] = value;
        }
    }
    file.close();

    auto it = kv_map.find("pair_device_num");
    if (it == kv_map.end()) {
        std::cerr << "[ParseRouteFile] Missing pair_device_num in: " << route_path << std::endl;
        return ERROR_FILE_PARSE_FAILED;
    }

    int pair_device_num = std::stoi(it->second);
    for (int i = 0; i < pair_device_num; ++i) {
        std::string dev_id_key = "pair" + std::to_string(i) + "_dev_id";
        auto dev_it = kv_map.find(dev_id_key);
        if (dev_it == kv_map.end()) continue;

        int device_id = std::stoi(dev_it->second);

        std::string chan_num_key = "pair" + std::to_string(i) + "_chan_num";
        int chan_num = 1;
        auto chan_num_it = kv_map.find(chan_num_key);
        if (chan_num_it != kv_map.end()) {
            chan_num = std::stoi(chan_num_it->second);
        }

        for (int j = 0; j < chan_num; ++j) {
            std::string local_key = "pair" + std::to_string(i) + "_chan" + std::to_string(j) + "_local_eid";
            std::string remote_key = "pair" + std::to_string(i) + "_chan" + std::to_string(j) + "_remote_eid";

            auto local_it = kv_map.find(local_key);
            auto remote_it = kv_map.find(remote_key);

            if (local_it != kv_map.end() && remote_it != kv_map.end()) {
                RouteEntry entry;
                entry.device_id = device_id;
                entry.local_eid = local_it->second;
                entry.remote_eid = remote_it->second;
                route_data.entries.push_back(entry);
            }
        }
    }

    std::cout << "[ParseRouteFile] Parsed " << route_data.entries.size() << " entries from " << route_path << std::endl;
    return SUCCESS;
}

// ============ 边生成实现 ============

int32_t GenerateD2DEdges(
    const TopoData& topo_data,
    const std::map<int32_t, NpuRootInfo>& npu_rootinfos,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    auto self_it = npu_rootinfos.find(phy_id);
    if (self_it == npu_rootinfos.end()) {
        std::cerr << "[GenerateD2DEdges] No rootinfo for self npu_id=" << phy_id << std::endl;
        return SUCCESS;
    }
    const auto& self_rootinfo = self_it->second;

    std::cout << "[GenerateD2DEdges] phy_id=" << phy_id
              << ", topo_links=" << topo_data.links.size()
              << ", self_rootinfo_size=" << self_rootinfo.port_to_eid.size() << std::endl;

    size_t skip_reason[4] = {0, 0, 0, 0};
    size_t no_rootinfo_peer = 0;
    size_t no_port_match_local = 0;
    size_t no_port_match_peer = 0;

    for (const auto& link : topo_data.links) {
        if (link.net_layer != 0) { ++skip_reason[0]; continue; }
        if (link.link_type != "PEER2PEER") { ++skip_reason[1]; continue; }
        if (link.topo_type != "1DMESH") { ++skip_reason[2]; continue; }
        if (link.local_a != phy_id) { ++skip_reason[3]; continue; }

        if (link.local_a_ports.empty() || link.local_b_ports.empty()) {
            std::cout << "[GenerateD2DEdges] skip (empty ports): local_a_ports=" << link.local_a_ports.size()
                      << ", local_b_ports=" << link.local_b_ports.size() << std::endl;
            continue;
        }

        std::string local_port = link.local_a_ports[0];
        std::string peer_port = link.local_b_ports[0];

        auto local_eid_it = self_rootinfo.port_to_eid.find(local_port);
        if (local_eid_it == self_rootinfo.port_to_eid.end()) {
            ++no_port_match_local;
            std::cout << "[GenerateD2DEdges] No EID for local port '" << local_port << "'" << std::endl;
            continue;
        }
        std::string comm_id = local_eid_it->second;

        int peer_id = link.local_b;
        auto peer_it = npu_rootinfos.find(peer_id);
        if (peer_it == npu_rootinfos.end()) {
            ++no_rootinfo_peer;
            std::cout << "[GenerateD2DEdges] No rootinfo for peer npu_id=" << peer_id << std::endl;
            continue;
        }
        auto peer_eid_it = peer_it->second.port_to_eid.find(peer_port);
        if (peer_eid_it == peer_it->second.port_to_eid.end()) {
            ++no_port_match_peer;
            std::cout << "[GenerateD2DEdges] No EID for peer port '" << peer_port << "' on npu_id=" << peer_id << std::endl;
            continue;
        }
        std::string dst_eid = peer_eid_it->second;

        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = comm_id;
        edge.placement = "device";
        edge.dst_eid = dst_eid;
        edges.push_back(edge);
        std::cout << "[GenerateD2DEdges] matched: comm_id=" << comm_id
                  << ", dst_eid=" << dst_eid << std::endl;
    }

    std::cout << "[GenerateD2DEdges] result: matched=" << edges.size()
              << ", skip(net_layer)=" << skip_reason[0]
              << ", skip(link_type)=" << skip_reason[1]
              << ", skip(topo_type)=" << skip_reason[2]
              << ", skip(phy_id)=" << skip_reason[3]
              << ", no_port_match_local=" << no_port_match_local
              << ", no_port_match_peer=" << no_port_match_peer
              << ", no_rootinfo_peer=" << no_rootinfo_peer << std::endl;
    return SUCCESS;
}

int32_t GenerateD2UEdges(
    const TopoData& topo_data,
    const std::map<int32_t, NpuRootInfo>& npu_rootinfos,
    const std::vector<std::string>& clos_plane_ids,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    auto self_it = npu_rootinfos.find(phy_id);
    if (self_it == npu_rootinfos.end()) {
        std::cerr << "[GenerateD2UEdges] No rootinfo for self npu_id=" << phy_id << std::endl;
        return SUCCESS;
    }
    const auto& self_rootinfo = self_it->second;

    std::cout << "[GenerateD2UEdges] phy_id=" << phy_id
              << ", clos_planes=" << clos_plane_ids.size()
              << ", topo_links=" << topo_data.links.size()
              << ", clos_pg_eid=" << (self_rootinfo.clos_pg_eid.empty() ? "(none)" : self_rootinfo.clos_pg_eid)
              << std::endl;

    size_t skip_reason[4] = {0, 0, 0, 0};
    size_t no_clos_eid = 0;
    for (const auto& link : topo_data.links) {
        if (link.net_layer != 1) { ++skip_reason[0]; continue; }
        if (link.link_type != "PEER2NET") { ++skip_reason[1]; continue; }
        if (link.topo_type != "CLOS") { ++skip_reason[2]; continue; }
        if (link.local_a != phy_id) { ++skip_reason[3]; continue; }

        // 从当前 NPU 的 rootinfo 中获取 CLOS 层 PG EID
        if (self_rootinfo.clos_pg_eid.empty()) {
            ++no_clos_eid;
            std::cout << "[GenerateD2UEdges] No CLOS PG EID in rootinfo for phy_id=" << phy_id << std::endl;
            continue;
        }

        for (const auto& plane_id : clos_plane_ids) {
            EndpointConfig edge;
            edge.protocol = "ub_ctp";
            edge.comm_id = self_rootinfo.clos_pg_eid;
            edge.placement = "device";
            edge.plane = plane_id;
            edges.push_back(edge);
            std::cout << "[GenerateD2UEdges] matched: comm_id=" << self_rootinfo.clos_pg_eid
                      << ", plane=" << plane_id << std::endl;
        }
    }

    std::cout << "[GenerateD2UEdges] result: matched=" << edges.size()
              << ", skip(net_layer)=" << skip_reason[0]
              << ", skip(link_type)=" << skip_reason[1]
              << ", skip(topo_type)=" << skip_reason[2]
              << ", skip(phy_id)=" << skip_reason[3]
              << ", no_clos_eid=" << no_clos_eid << std::endl;
    return SUCCESS;
}

int32_t GenerateH2DEdges(
    const RouteData& route_data,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    std::cout << "[GenerateH2DEdges] phy_id=" << phy_id
              << ", route_entries=" << route_data.entries.size() << std::endl;

    size_t skip_phy_id = 0;
    for (const auto& entry : route_data.entries) {
        if (entry.device_id != phy_id) { ++skip_phy_id; continue; }

        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = entry.local_eid;
        edge.placement = "host";
        edge.dst_eid = entry.remote_eid;
        edges.push_back(edge);
        std::cout << "[GenerateH2DEdges] matched: local_eid=" << entry.local_eid
                  << ", remote_eid=" << entry.remote_eid << std::endl;
    }

    std::cout << "[GenerateH2DEdges] result: matched=" << edges.size()
              << ", skip(phy_id)=" << skip_phy_id << std::endl;
    return SUCCESS;
}

int32_t GenerateH2UEdges(
    const RouteData& route_data,
    const std::vector<std::string>& clos_plane_ids,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    std::cout << "[GenerateH2UEdges] phy_id=" << phy_id
              << ", route_entries=" << route_data.entries.size()
              << ", clos_plane_ids=" << clos_plane_ids.size() << std::endl;

    size_t skip_phy_id = 0;
    for (const auto& entry : route_data.entries) {
        if (entry.device_id != phy_id) { ++skip_phy_id; continue; }

        for (const auto& plane_id : clos_plane_ids) {
            EndpointConfig edge;
            edge.protocol = "ub_ctp";
            edge.comm_id = entry.local_eid;
            edge.placement = "host";
            edge.plane = plane_id;
            edges.push_back(edge);
            std::cout << "[GenerateH2UEdges] matched: local_eid=" << entry.local_eid
                      << ", plane=" << plane_id << std::endl;
        }
    }

    std::cout << "[GenerateH2UEdges] result: matched=" << edges.size()
              << ", skip(phy_id)=" << skip_phy_id << std::endl;
    return SUCCESS;
}

// ============ 核心接口实现 ============

int32_t GenerateLocalCommRes(
    int32_t phy_dev_id,
    const std::map<std::string, std::string>& options,
    LocalCommRes& local_comm_res) {

    std::cout << "[GenerateLocalCommRes] ===== Start =====" << std::endl;
    std::cout << "[GenerateLocalCommRes] phy_dev_id=" << phy_dev_id << std::endl;

    std::string topo_path;
    std::string route_path;

    auto it = options.find("topo_path");
    if (it != options.end()) {
        topo_path = it->second;
    }

    it = options.find("route_path");
    if (it != options.end()) {
        route_path = it->second;
    }

    std::cout << "[GenerateLocalCommRes] topo_path=" << topo_path << std::endl;
    std::cout << "[GenerateLocalCommRes] route_path=" << route_path << std::endl;

    // 获取主板 ID
    unsigned int mainboard_id = 0;
    int32_t ret = GetMainboardId(phy_dev_id, mainboard_id);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] Failed to get mainboard id: " << ret << std::endl;
    } else {
        std::cout << "[GenerateLocalCommRes] mainboard_id=0x" << std::hex << mainboard_id << std::dec << std::endl;
    }

    // 根据 mainboard_id 判断产品形态
    constexpr unsigned int MAIN_BOARD_ID_POD_2D = 0x03;
    constexpr unsigned int MAIN_BOARD_ID_SERVER_TYPE1 = 0x23;
    constexpr unsigned int MAIN_BOARD_ID_SERVER_8PMESH = 0x25;
    constexpr unsigned int MAIN_BOARD_ID_SERVER_16PMESH = 0x44;
    bool is_pod = (mainboard_id == MAIN_BOARD_ID_POD_2D);
    bool is_server = (mainboard_id == MAIN_BOARD_ID_SERVER_TYPE1 ||
                      mainboard_id == MAIN_BOARD_ID_SERVER_8PMESH ||
                      mainboard_id == MAIN_BOARD_ID_SERVER_16PMESH);
    if (is_pod) {
        std::cout << "[GenerateLocalCommRes] Product type: Pod (mainboard_id=0x" << std::hex << mainboard_id << std::dec << ")" << std::endl;
    } else if (is_server) {
        std::cout << "[GenerateLocalCommRes] Product type: Server (mainboard_id=0x" << std::hex << mainboard_id << std::dec << ")" << std::endl;
    } else {
        std::cout << "[GenerateLocalCommRes] Product type: Unknown/default (mainboard_id=0x" << std::hex << mainboard_id << std::dec << ")" << std::endl;
    }

    // 解析 topo 文件
    TopoData topo_data;
    ret = ParseTopoFile(topo_path, topo_data);
    if (ret != SUCCESS && ret != ERROR_FILE_NOT_FOUND) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse topo file: " << ret << std::endl;
    } else {
        std::cout << "[GenerateLocalCommRes] ParseTopoFile " << (ret == SUCCESS ? "ok" : "not found") << ", links=" << topo_data.links.size() << std::endl;
    }

    // 解析 route 文件
    RouteData route_data;
    ret = ParseRouteFile(route_path, route_data);
    if (ret != SUCCESS && ret != ERROR_FILE_NOT_FOUND) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse route file: " << ret << std::endl;
    } else {
        std::cout << "[GenerateLocalCommRes] ParseRouteFile " << (ret == SUCCESS ? "ok" : "not found") << ", entries=" << route_data.entries.size() << std::endl;
    }

    // 收集所有相关 NPU ID（从 topo 边中的 local_a 和 local_b）
    std::set<int32_t> related_npu_ids;
    related_npu_ids.insert(phy_dev_id);
    for (const auto& link : topo_data.links) {
        related_npu_ids.insert(link.local_a);
        related_npu_ids.insert(link.local_b);
    }
    std::cout << "[GenerateLocalCommRes] Related NPU IDs: ";
    for (int id : related_npu_ids) std::cout << id << " ";
    std::cout << std::endl;

    // 为每个相关 NPU 构建 rootinfo
    std::map<int32_t, NpuRootInfo> npu_rootinfos;
    for (int32_t npu_id : related_npu_ids) {
        NpuRootInfo rootinfo;
        int32_t build_ret = BuildNpuRootInfo(npu_id, is_server, rootinfo);
        if (build_ret != SUCCESS) {
            std::cerr << "[GenerateLocalCommRes] Failed to build rootinfo for npu_id=" << npu_id << ", ret=" << build_ret << std::endl;
            continue;
        }
        npu_rootinfos[npu_id] = rootinfo;
    }
    std::cout << "[GenerateLocalCommRes] Built rootinfo for " << npu_rootinfos.size() << " NPU(s)" << std::endl;

    // 获取当前 NPU 的 CLOS pg_eid（用于 D2U 和 H2U）
    std::string clos_pg_eid;
    std::vector<std::string> clos_plane_ids;
    auto self_it = npu_rootinfos.find(phy_dev_id);
    if (self_it != npu_rootinfos.end() && !self_it->second.clos_pg_eid.empty()) {
        clos_pg_eid = self_it->second.clos_pg_eid;
        clos_plane_ids.push_back("plane_pg_0");
        clos_plane_ids.push_back("plane_pg_1");
    }
    std::cout << "[GenerateLocalCommRes] CLOS pg_eid=" << (clos_pg_eid.empty() ? "(none)" : clos_pg_eid) << std::endl;

    std::vector<EndpointConfig> all_edges;

    // D2D 边
    std::vector<EndpointConfig> d2d_edges;
    if (topo_data.links.empty()) {
        std::cout << "[GenerateLocalCommRes] Skip D2D: topo_data.links is empty" << std::endl;
    } else if (npu_rootinfos.empty()) {
        std::cout << "[GenerateLocalCommRes] Skip D2D: no rootinfo available" << std::endl;
    } else {
        GenerateD2DEdges(topo_data, npu_rootinfos, phy_dev_id, d2d_edges);
        all_edges.insert(all_edges.end(), d2d_edges.begin(), d2d_edges.end());
    }
    std::cout << "[GenerateLocalCommRes] D2D edges=" << d2d_edges.size() << std::endl;

    // D2U 边
    std::vector<EndpointConfig> d2u_edges;
    if (clos_plane_ids.empty()) {
        std::cout << "[GenerateLocalCommRes] Skip D2U: no CLOS plane_ids found" << std::endl;
    } else if (topo_data.links.empty()) {
        std::cout << "[GenerateLocalCommRes] Skip D2U: topo_data.links is empty" << std::endl;
    } else {
        GenerateD2UEdges(topo_data, npu_rootinfos, clos_plane_ids, phy_dev_id, d2u_edges);
        all_edges.insert(all_edges.end(), d2u_edges.begin(), d2u_edges.end());
    }
    std::cout << "[GenerateLocalCommRes] D2U edges=" << d2u_edges.size() << std::endl;

    // H2D 边
    std::vector<EndpointConfig> h2d_edges;
    if (route_data.entries.empty()) {
        std::cout << "[GenerateLocalCommRes] Skip H2D: route_data.entries is empty" << std::endl;
    } else {
        GenerateH2DEdges(route_data, phy_dev_id, h2d_edges);
        all_edges.insert(all_edges.end(), h2d_edges.begin(), h2d_edges.end());
    }
    std::cout << "[GenerateLocalCommRes] H2D edges=" << h2d_edges.size() << std::endl;

    // H2U 边
    std::vector<EndpointConfig> h2u_edges;
    if (clos_plane_ids.empty()) {
        std::cout << "[GenerateLocalCommRes] Skip H2U: no CLOS plane_ids found" << std::endl;
    } else if (route_data.entries.empty()) {
        std::cout << "[GenerateLocalCommRes] Skip H2U: route_data.entries is empty" << std::endl;
    } else {
        GenerateH2UEdges(route_data, clos_plane_ids, phy_dev_id, h2u_edges);
        all_edges.insert(all_edges.end(), h2u_edges.begin(), h2u_edges.end());
    }
    std::cout << "[GenerateLocalCommRes] H2U edges=" << h2u_edges.size() << std::endl;

    local_comm_res.version = "1.3";
    local_comm_res.net_instance_id = "";
    local_comm_res.endpoint_list = all_edges;

    std::cout << "[GenerateLocalCommRes] Total endpoints=" << all_edges.size() << ", Done" << std::endl;

    return SUCCESS;
}

}  // namespace hixl
