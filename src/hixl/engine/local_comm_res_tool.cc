/**
 * @file local_comm_res_tool.cc
 * @brief LocalCommRes 生成工具实现文件
 *
 * 本文件负责：
 * - DCMI 接口封装（GetUBEntityList, GetMainboardId）
 * - 文件解析（ParseTopoFile, ParseRouteFile）
 * - 边生成（GenerateD2DEdges, GenerateH2DEdges, GenerateD2HEdges）
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
#include <dirent.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <unistd.h>

namespace hixl {

// ============ 产品形态常量 ============

namespace {
constexpr uint32_t kMainboardIdPod1 = 0x3;
constexpr uint32_t kMainboardIdPod2 = 0x5;
constexpr uint32_t kMainboardIdPod3 = 0x7;
constexpr uint32_t kMainboardIdServerMin1 = 0x21;
constexpr uint32_t kMainboardIdServerMax1 = 0x2B;
constexpr uint32_t kMainboardIdServerMin2 = 0x40;
constexpr uint32_t kMainboardIdServerMax2 = 0x46;
}  // anonymous namespace

// ============ DCMI 接口动态加载 ============

namespace {

// DCMI SPOD 信息结构体
struct DcmiSpodInfo {
    unsigned int sdid;
    unsigned int super_pod_size;
    unsigned int super_pod_id;
    unsigned int server_index;
    unsigned int chassis_id;
    unsigned int super_pod_type;
    unsigned int reserve[6];
};

// DCMI 主命令和子命令
enum DcmiMainCmd {
    DCMI_MAIN_CMD_CHIP_INF = 12,
};

enum DcmiChipInfoSubCmd {
    DCMI_CHIP_INFO_SUB_CMD_SPOD_INFO = 1,
};

// DCMI 接口函数指针类型
typedef int (*dcmi_init_func)();
typedef int (*dcmi_get_urma_device_cnt_func)(int npu_id, unsigned int* dev_cnt);
typedef int (*dcmi_get_eid_list_func)(int npu_id, int urma_dev_index,
                                       dcmi_urma_eid_info_t* eid_list, int* eid_cnt);
typedef int (*dcmi_get_mainboard_id_func)(int npu_id, unsigned int* mainboard_id);
typedef int (*dcmi_get_logicid_from_phyid_func)(unsigned int phy_id, unsigned int* logic_id);
typedef int (*dcmi_get_device_info_func)(int npu_id, int main_cmd,
                                          unsigned int sub_cmd, void* buf, unsigned int* size);

// DCMI 接口函数指针（全局）
dcmi_init_func g_dcmi_init = nullptr;
dcmi_get_urma_device_cnt_func g_dcmi_get_urma_device_cnt = nullptr;
dcmi_get_eid_list_func g_dcmi_get_eid_list = nullptr;
dcmi_get_mainboard_id_func g_dcmi_get_mainboard_id = nullptr;
dcmi_get_logicid_from_phyid_func g_dcmi_get_logicid_from_phyid = nullptr;
dcmi_get_device_info_func g_dcmi_get_device_info = nullptr;

// DCMI 库句柄
void* g_dcmi_handle = nullptr;

// 加载状态
volatile bool g_dcmi_loaded = false;
volatile int g_dcmi_init_status = -1;

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

// 在 /etc/ 目录下模糊匹配以 noroce.json 结尾的文件，返回修改时间最新的一个
std::string FindLatestTopoFile() {
    const char* dir_path = "/etc/";
    const char* suffix = "noroce.json";
    DIR* dir = opendir(dir_path);
    if (!dir) {
        std::cerr << "[FindLatestTopoFile] Failed to open /etc/ for topo file scan" << std::endl;
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

}  // anonymous namespace

// ============ 文件解析辅助函数 ============

bool LoadRouteKvMap(std::ifstream& file, std::map<std::string, std::string>& kv_map) {
    std::string line;
    while (std::getline(file, line)) {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        size_t key_end = key.find_last_not_of(" \t\r\n");
        if (key_end != std::string::npos) key = key.substr(0, key_end + 1);

        size_t val_start = value.find_first_not_of(" \t\r\n");
        size_t val_end = value.find_last_not_of(" \t\r\n");
        if (val_start != std::string::npos && val_end != std::string::npos) {
            value = value.substr(val_start, val_end - val_start + 1);
        }
        kv_map[key] = value;
    }
    return true;
}

void AddRouteEntriesForDevice(const std::map<std::string, std::string>& kv_map,
                              int device_idx,
                              int device_id,
                              RouteData& route_data) {
    std::string chan_num_key = "pair" + std::to_string(device_idx) + "_chan_num";
    int chan_num = 1;
    auto chan_num_it = kv_map.find(chan_num_key);
    if (chan_num_it != kv_map.end()) {
        chan_num = std::stoi(chan_num_it->second);
    }

    for (int j = 0; j < chan_num; ++j) {
        std::string local_key = "pair" + std::to_string(device_idx) + "_chan" + std::to_string(j) + "_local_eid";
        std::string remote_key = "pair" + std::to_string(device_idx) + "_chan" + std::to_string(j) + "_remote_eid";

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

int32_t BuildRouteEntries(const std::map<std::string, std::string>& kv_map, RouteData& route_data) {
    auto it = kv_map.find("pair_device_num");
    if (it == kv_map.end()) {
        std::cerr << "[BuildRouteEntries] Missing pair_device_num" << std::endl;
        return ERROR_FILE_PARSE_FAILED;
    }

    int pair_device_num = std::stoi(it->second);
    for (int i = 0; i < pair_device_num; ++i) {
        std::string dev_id_key = "pair" + std::to_string(i) + "_dev_id";
        auto dev_it = kv_map.find(dev_id_key);
        if (dev_it == kv_map.end()) continue;

        int device_id = std::stoi(dev_it->second);
        AddRouteEntriesForDevice(kv_map, i, device_id, route_data);
    }

    std::cout << "[BuildRouteEntries] Parsed " << route_data.entries.size() << " entries" << std::endl;
    return SUCCESS;
}

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

int32_t GetClosNetInstanceId(int32_t phy_dev_id, std::string& net_instance_id) {
    if (LoadDcmi() != 0) {
        std::cerr << "[GetClosNetInstanceId] DCMI not loaded" << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int logic_id = 0;
    if (GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
        std::cerr << "[GetClosNetInstanceId] Failed to get logic id from phy id: " << phy_dev_id << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    DcmiSpodInfo spod_info;
    memset(&spod_info, 0, sizeof(spod_info));
    unsigned int buf_size = sizeof(DcmiSpodInfo);
    int ret = g_dcmi_get_device_info(logic_id, DCMI_MAIN_CMD_CHIP_INF,
                                     DCMI_CHIP_INFO_SUB_CMD_SPOD_INFO, &spod_info, &buf_size);
    if (ret != 0) {
        std::cerr << "[GetClosNetInstanceId] Failed to get device info, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    net_instance_id = "superpod_" + std::to_string(spod_info.super_pod_id);
    std::cout << "[GetClosNetInstanceId] phy_dev_id=" << phy_dev_id
              << ", super_pod_id=" << spod_info.super_pod_id
              << ", net_instance_id=" << net_instance_id << std::endl;
    return SUCCESS;
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
    if (!LoadRouteKvMap(file, kv_map)) {
        std::cerr << "[ParseRouteFile] Failed to load kv map" << std::endl;
        return ERROR_FILE_PARSE_FAILED;
    }
    file.close();

    return BuildRouteEntries(kv_map, route_data);
}

// ============ 边生成实现 ============

void AddD2DEdgesFromLink(const NpuRootInfo& self_rootinfo,
                         const NpuRootInfo& peer_rootinfo,
                         int peer_id,
                         const std::vector<std::string>& local_ports,
                         const std::vector<std::string>& peer_ports,
                         std::vector<EndpointConfig>& edges,
                         size_t& no_port_match_local,
                         size_t& no_port_match_peer) {
    for (size_t i = 0; i < local_ports.size() && i < peer_ports.size(); ++i) {
        std::string local_port = local_ports[i];
        std::string peer_port = peer_ports[i];

        auto local_eid_it = self_rootinfo.port_to_eid.find(local_port);
        if (local_eid_it == self_rootinfo.port_to_eid.end()) {
            ++no_port_match_local;
            std::cout << "[AddD2DEdgesFromLink] No EID for local port '" << local_port << "'" << std::endl;
            continue;
        }
        std::string comm_id = local_eid_it->second;

        auto peer_eid_it = peer_rootinfo.port_to_eid.find(peer_port);
        if (peer_eid_it == peer_rootinfo.port_to_eid.end()) {
            ++no_port_match_peer;
            std::cout << "[AddD2DEdgesFromLink] No EID for peer port '" << peer_port << "' on npu_id=" << peer_id << std::endl;
            continue;
        }
        std::string dst_eid = peer_eid_it->second;

        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = comm_id;
        edge.placement = "device";
        edge.dst_eid = dst_eid;
        edges.push_back(edge);
        std::cout << "[AddD2DEdgesFromLink] matched: comm_id=" << comm_id
                  << ", dst_eid=" << dst_eid << std::endl;
    }
}

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

        bool is_local_a_side = (link.local_a == phy_id);
        bool is_local_b_side = (link.local_b == phy_id);
        if (!is_local_a_side && !is_local_b_side) { ++skip_reason[3]; continue; }

        int peer_id = is_local_a_side ? link.local_b : link.local_a;
        const std::vector<std::string>& local_ports = is_local_a_side ? link.local_a_ports : link.local_b_ports;
        const std::vector<std::string>& peer_ports = is_local_a_side ? link.local_b_ports : link.local_a_ports;

        if (local_ports.empty() || peer_ports.empty()) {
            std::cout << "[GenerateD2DEdges] skip (empty ports)" << std::endl;
            continue;
        }

        auto peer_it = npu_rootinfos.find(peer_id);
        if (peer_it == npu_rootinfos.end()) {
            ++no_rootinfo_peer;
            std::cout << "[GenerateD2DEdges] No rootinfo for peer npu_id=" << peer_id << std::endl;
            continue;
        }
        const auto& peer_rootinfo = peer_it->second;

        AddD2DEdgesFromLink(self_rootinfo, peer_rootinfo, peer_id,
                           local_ports, peer_ports, edges,
                           no_port_match_local, no_port_match_peer);
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

int32_t GenerateH2DEdges(
    const RouteData& route_data,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    std::cout << "[GenerateH2DEdges] route_entries=" << route_data.entries.size() << std::endl;

    for (const auto& entry : route_data.entries) {
        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = entry.local_eid;
        edge.placement = "host";
        edge.dst_eid = entry.remote_eid;
        edges.push_back(edge);
        std::cout << "[GenerateH2DEdges] matched: device_id=" << entry.device_id
                  << ", local_eid=" << entry.local_eid << ", remote_eid=" << entry.remote_eid << std::endl;
    }

    std::cout << "[GenerateH2DEdges] result: matched=" << edges.size() << std::endl;
    return SUCCESS;
}

int32_t GenerateD2HEdges(
    const RouteData& route_data,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    std::cout << "[GenerateD2HEdges] route_entries=" << route_data.entries.size() << std::endl;

    for (const auto& entry : route_data.entries) {
        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = entry.remote_eid;
        edge.placement = "device";
        edge.dst_eid = entry.local_eid;
        edges.push_back(edge);
        std::cout << "[GenerateD2HEdges] matched: device_id=" << entry.device_id
                  << ", remote_eid=" << entry.remote_eid << ", local_eid=" << entry.local_eid << std::endl;
    }

    std::cout << "[GenerateD2HEdges] result: matched=" << edges.size() << std::endl;
    return SUCCESS;
}

// ============ 核心接口实现 ============

bool IsProductPod(uint32_t mainboard_id) {
    return (mainboard_id == kMainboardIdPod1 || mainboard_id == kMainboardIdPod2 || mainboard_id == kMainboardIdPod3);
}

bool IsProductServer(uint32_t mainboard_id) {
    return ((mainboard_id >= kMainboardIdServerMin1 && mainboard_id <= kMainboardIdServerMax1 && (mainboard_id % 2 == 1)) ||
           (mainboard_id >= kMainboardIdServerMin2 && mainboard_id <= kMainboardIdServerMax2 && (mainboard_id % 2 == 0)));
}

std::set<int32_t> CollectRelatedNpuIds(int32_t phy_dev_id, const TopoData& topo_data) {
    (void)topo_data;
    // NPU 按 8 个一组划分，找出 phy_dev_id 所在组的所有 NPU
    int32_t group_start = (phy_dev_id / 8) * 8;
    std::set<int32_t> related_npu_ids;
    for (int32_t i = 0; i < 8; ++i) {
        related_npu_ids.insert(group_start + i);
    }
    std::cout << "[CollectRelatedNpuIds] phy_dev_id=" << phy_dev_id
              << ", group_start=" << group_start << ", Related NPU IDs: ";
    for (int id : related_npu_ids) std::cout << id << " ";
    std::cout << std::endl;
    return related_npu_ids;
}

int32_t BuildNpuRootinfos(const std::set<int32_t>& related_npu_ids,
                          bool is_server,
                          const std::string& eid_json_path,
                          std::map<int32_t, NpuRootInfo>& npu_rootinfos) {
    npu_rootinfos.clear();
    for (int32_t npu_id : related_npu_ids) {
        NpuRootInfo rootinfo;
        int32_t build_ret = BuildNpuRootInfo(npu_id, is_server, rootinfo, eid_json_path);
        if (build_ret != SUCCESS) {
            std::cerr << "[BuildNpuRootinfos] Failed to build rootinfo for npu_id=" << npu_id << ", ret=" << build_ret << std::endl;
            return build_ret;
        }
        npu_rootinfos[npu_id] = rootinfo;
    }
    std::cout << "[BuildNpuRootinfos] Built rootinfo for " << npu_rootinfos.size() << " NPU(s)" << std::endl;
    return SUCCESS;
}

void CollectClosPgEids(const std::map<int32_t, NpuRootInfo>& npu_rootinfos,
                       int32_t phy_dev_id,
                       bool is_server,
                       std::string& plane_pg_0_eid,
                       std::string& plane_pg_1_eid,
                       int& mesh_die_id) {
    mesh_die_id = GetMeshDieId(phy_dev_id, is_server);
    auto self_it = npu_rootinfos.find(phy_dev_id);
    if (self_it == npu_rootinfos.end()) return;

    // rootinfo_builder 已按正确逻辑过滤：clos_pg_eids[0]=plane_pg_0, [1]=plane_pg_1
    const auto& pg_eids = self_it->second.clos_pg_eids;
    if (pg_eids.size() >= 1) {
        plane_pg_0_eid = pg_eids[0].eid;
    }
    if (pg_eids.size() >= 2) {
        plane_pg_1_eid = pg_eids[1].eid;
    }
    std::cout << "[CollectClosPgEids] Mesh die_id=" << mesh_die_id << std::endl;
    std::cout << "[CollectClosPgEids] plane_pg_0_eid=" << (plane_pg_0_eid.empty() ? "(none)" : plane_pg_0_eid) << std::endl;
    std::cout << "[CollectClosPgEids] plane_pg_1_eid=" << (plane_pg_1_eid.empty() ? "(none)" : plane_pg_1_eid) << std::endl;
}

void GenerateD2UEdges(const std::string& plane_pg_0_eid,
                      const std::string& plane_pg_1_eid,
                      std::vector<EndpointConfig>& d2u_edges) {
    if (!plane_pg_0_eid.empty()) {
        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = plane_pg_0_eid;
        edge.placement = "device";
        edge.plane = "plane_pg_0";
        d2u_edges.push_back(edge);
    }
    if (!plane_pg_1_eid.empty()) {
        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = plane_pg_1_eid;
        edge.placement = "device";
        edge.plane = "plane_pg_1";
        d2u_edges.push_back(edge);
    }
}

void GenerateH2UEdges(const std::string& plane_pg_0_eid,
                       const std::string& plane_pg_1_eid,
                       std::vector<EndpointConfig>& h2u_edges) {
    if (!plane_pg_0_eid.empty()) {
        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = plane_pg_0_eid;
        edge.placement = "host";
        edge.plane = "plane_pg_0";
        h2u_edges.push_back(edge);
    }
    if (!plane_pg_1_eid.empty()) {
        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = plane_pg_1_eid;
        edge.placement = "host";
        edge.plane = "plane_pg_1";
        h2u_edges.push_back(edge);
    }
}

int32_t GenerateLocalCommRes(
    int32_t phy_dev_id,
    const std::map<std::string, std::string>& options,
    LocalCommRes& local_comm_res) {

    std::cout << "[GenerateLocalCommRes] ===== Start =====" << std::endl;
    std::cout << "[GenerateLocalCommRes] phy_dev_id=" << phy_dev_id << std::endl;

    std::string topo_path;
    std::string route_path;
    std::string eid_json_path;

    auto it = options.find("topo_path");
    if (it != options.end()) topo_path = it->second;
    it = options.find("route_path");
    if (it != options.end()) route_path = it->second;
    it = options.find("eid_json_path");
    if (it != options.end()) eid_json_path = it->second;

    // 1. 默认路径查找逻辑
    if (topo_path.empty()) {
        topo_path = FindLatestTopoFile();
        if (topo_path.empty()) {
            std::cerr << "[GenerateLocalCommRes] No topo file specified and none found in /etc/" << std::endl;
            return ERROR_FILE_NOT_FOUND;
        }
        std::cout << "[GenerateLocalCommRes] Using default topo_path=" << topo_path << std::endl;
    }
    if (route_path.empty()) {
        route_path = "/lib/route.conf";
        std::cout << "[GenerateLocalCommRes] Using default route_path=" << route_path << std::endl;
    }

    std::cout << "[GenerateLocalCommRes] topo_path=" << topo_path << std::endl;
    std::cout << "[GenerateLocalCommRes] route_path=" << route_path << std::endl;
    std::cout << "[GenerateLocalCommRes] eid_json_path=" << eid_json_path << std::endl;

    // 2. GetMainboardId 失败 → 终止
    unsigned int mainboard_id = 0;
    int32_t ret = GetMainboardId(phy_dev_id, mainboard_id);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] Failed to get mainboard id: " << ret << std::endl;
        return ret;
    }
    std::cout << "[GenerateLocalCommRes] mainboard_id=0x" << std::hex << mainboard_id << std::dec << std::endl;

    bool is_pod = IsProductPod(mainboard_id);
    bool is_server = IsProductServer(mainboard_id);
    if (is_pod) {
        std::cout << "[GenerateLocalCommRes] Product type: Pod" << std::endl;
    } else if (is_server) {
        std::cout << "[GenerateLocalCommRes] Product type: Server" << std::endl;
    } else {
        std::cout << "[GenerateLocalCommRes] Product type: Unknown/default" << std::endl;
    }

    // 3. topo 文件解析失败 → 终止
    TopoData topo_data;
    ret = ParseTopoFile(topo_path, topo_data);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse topo file: " << ret << std::endl;
        return ret;
    }
    std::cout << "[GenerateLocalCommRes] ParseTopoFile ok" << std::endl;

    // 4. route 文件解析失败 → 终止
    RouteData route_data;
    ret = ParseRouteFile(route_path, route_data);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse route file: " << ret << std::endl;
        return ret;
    }
    std::cout << "[GenerateLocalCommRes] ParseRouteFile ok" << std::endl;

    // 5. BuildNpuRootinfos 任何 NPU 失败 → 终止
    std::set<int32_t> related_npu_ids = CollectRelatedNpuIds(phy_dev_id, topo_data);
    std::map<int32_t, NpuRootInfo> npu_rootinfos;
    ret = BuildNpuRootinfos(related_npu_ids, is_server, eid_json_path, npu_rootinfos);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] BuildNpuRootinfos failed: " << ret << std::endl;
        return ret;
    }

    std::string plane_pg_0_eid;
    std::string plane_pg_1_eid;
    int mesh_die_id = 0;
    CollectClosPgEids(npu_rootinfos, phy_dev_id, is_server, plane_pg_0_eid, plane_pg_1_eid, mesh_die_id);

    std::vector<EndpointConfig> all_edges;

    std::vector<EndpointConfig> d2d_edges;
    if (!topo_data.links.empty() && !npu_rootinfos.empty()) {
        GenerateD2DEdges(topo_data, npu_rootinfos, phy_dev_id, d2d_edges);
    }
    all_edges.insert(all_edges.end(), d2d_edges.begin(), d2d_edges.end());
    std::cout << "[GenerateLocalCommRes] D2D edges=" << d2d_edges.size() << std::endl;

    std::vector<EndpointConfig> d2u_edges;
    if (!plane_pg_0_eid.empty() || !plane_pg_1_eid.empty()) {
        GenerateD2UEdges(plane_pg_0_eid, plane_pg_1_eid, d2u_edges);
    }
    all_edges.insert(all_edges.end(), d2u_edges.begin(), d2u_edges.end());
    std::cout << "[GenerateLocalCommRes] D2U edges=" << d2u_edges.size() << std::endl;

    std::vector<EndpointConfig> h2d_edges;
    if (!route_data.entries.empty()) {
        GenerateH2DEdges(route_data, h2d_edges);
    }
    all_edges.insert(all_edges.end(), h2d_edges.begin(), h2d_edges.end());
    std::cout << "[GenerateLocalCommRes] H2D edges=" << h2d_edges.size() << std::endl;

    std::vector<EndpointConfig> d2h_edges;
    if (!route_data.entries.empty()) {
        GenerateD2HEdges(route_data, d2h_edges);
    }
    all_edges.insert(all_edges.end(), d2h_edges.begin(), d2h_edges.end());
    std::cout << "[GenerateLocalCommRes] D2H edges=" << d2h_edges.size() << std::endl;

    std::vector<EndpointConfig> h2u_edges;
    if (!plane_pg_0_eid.empty() || !plane_pg_1_eid.empty()) {
        GenerateH2UEdges(plane_pg_0_eid, plane_pg_1_eid, h2u_edges);
    }
    all_edges.insert(all_edges.end(), h2u_edges.begin(), h2u_edges.end());
    std::cout << "[GenerateLocalCommRes] H2U edges=" << h2u_edges.size() << std::endl;

    // 7. all_edges 为空 → 终止
    if (all_edges.empty()) {
        std::cerr << "[GenerateLocalCommRes] No edges generated, aborting" << std::endl;
        return ERROR_FILE_NOT_FOUND;
    }

    local_comm_res.version = "1.3";

    // 6. GetClosNetInstanceId 失败 → 终止
    std::string net_instance_id;
    int32_t net_ret = GetClosNetInstanceId(phy_dev_id, net_instance_id);
    if (net_ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] GetClosNetInstanceId failed: " << net_ret << std::endl;
        return net_ret;
    }
    local_comm_res.net_instance_id = net_instance_id;
    local_comm_res.endpoint_list = all_edges;

    // 为每个 endpoint 设置 net_instance_id
    for (auto& ep : local_comm_res.endpoint_list) {
        ep.net_instance_id = local_comm_res.net_instance_id;
    }

    std::cout << "[GenerateLocalCommRes] Total endpoints=" << all_edges.size() << ", Done" << std::endl;
    return SUCCESS;
}

}  // namespace hixl
