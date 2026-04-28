/**
 * @file local_comm_res_tool.cc
 * @brief LocalCommRes 生成工具实现文件
 *
 * DCMI 接口通过 dlopen 方式动态加载 libdcmi.so
 */

#include "local_comm_res_tool.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iomanip>
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

}  // anonymous namespace

// ============ DCMI 接口封装实现 ============

int32_t GetEidListByPhyId(int32_t phy_dev_id, std::vector<std::string>& eid_list) {
    if (LoadDcmi() != 0) {
        std::cerr << "[GetEidListByPhyId] DCMI not loaded" << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int logic_id = 0;
    if (GetLogicIdFromPhyId(phy_dev_id, &logic_id) != 0) {
        std::cerr << "[GetEidListByPhyId] Failed to get logic id from phy id: " << phy_dev_id << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    unsigned int dev_cnt = 0;
    int ret = g_dcmi_get_urma_device_cnt(logic_id, &dev_cnt);
    if (ret != 0) {
        std::cerr << "[GetEidListByPhyId] Failed to get urma device count, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    eid_list.clear();
    dcmi_urma_eid_info_t eid_list_buf[MAX_EID_NUM];
    size_t eid_current_cnt = 0;
    size_t eid_space_left = MAX_EID_NUM;

    for (size_t i = 0; i < dev_cnt && eid_space_left > 0; ++i) {
        int left = static_cast<int>(eid_space_left);
        ret = g_dcmi_get_eid_list(logic_id, i, &eid_list_buf[eid_current_cnt], &left);
        if (ret != 0) {
            continue;
        }
        eid_space_left -= left;
        eid_current_cnt += left;
    }

    for (size_t i = 0; i < eid_current_cnt; ++i) {
        eid_list.push_back(EidToString(eid_list_buf[i].eid));
    }

    return SUCCESS;
}

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

// ============ EID 解析实现 ============

std::string EidToString(const dcmi_urma_eid_t& eid) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << eid.in6.interface_id;
    return oss.str();
}

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

// ============ CLOS 层专用接口实现 ============

std::string GetPgEid(const std::vector<std::string>& eid_list) {
    for (const auto& eid : eid_list) {
        if (IsClosLayerEid(eid)) {
            return eid;
        }
    }
    return "";
}

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
    (void)topo_data;  // TODO: 实现解析逻辑后使用
    std::ifstream file(topo_path);
    if (!file.is_open()) {
        std::cerr << "[ParseTopoFile] Failed to open file: " << topo_path << std::endl;
        return ERROR_FILE_NOT_FOUND;
    }

    // TODO: 实现 JSON 解析逻辑

    file.close();
    return SUCCESS;
}

int32_t ParseRouteFile(const std::string& route_path, RouteData& route_data) {
    (void)route_data;  // TODO: 实现解析逻辑后使用
    std::ifstream file(route_path);
    if (!file.is_open()) {
        std::cerr << "[ParseRouteFile] Failed to open file: " << route_path << std::endl;
        return ERROR_FILE_NOT_FOUND;
    }

    // TODO: 实现 route.conf 解析逻辑

    file.close();
    return SUCCESS;
}

// ============ 边生成实现 ============

int32_t GenerateD2DEdges(
    const TopoData& topo_data,
    const std::vector<std::string>& eid_list,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    (void)eid_list;  // TODO: 实现时可能需要使用
    edges.clear();

    for (const auto& link : topo_data.links) {
        if (link.net_layer != 0) continue;
        if (link.link_type != "PEER2PEER") continue;
        if (link.topo_type != "1DMESH") continue;
        if (link.local_a != phy_id) continue;

        std::string comm_id = link.local_a_ports;
        std::string dst_eid = link.local_b_ports;

        if (IsMeshLayerEid(comm_id) && IsMeshLayerEid(dst_eid)) {
            EndpointConfig edge;
            edge.protocol = "ub_ctp";
            edge.comm_id = comm_id;
            edge.placement = "device";
            edge.dst_eid = dst_eid;
            edges.push_back(edge);
        }
    }

    return SUCCESS;
}

int32_t GenerateD2UEdges(
    const TopoData& topo_data,
    const std::string& clos_pg_eid,
    const std::vector<std::string>& clos_ports,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    if (clos_pg_eid.empty()) {
        return SUCCESS;
    }

    for (const auto& link : topo_data.links) {
        if (link.net_layer != 1) continue;
        if (link.link_type != "PEER2NET") continue;
        if (link.topo_type != "CLOS") continue;
        if (link.local_a != phy_id) continue;

        for (const auto& plane_id : clos_ports) {
            EndpointConfig edge;
            edge.protocol = "ub_ctp";
            edge.comm_id = clos_pg_eid;
            edge.placement = "device";
            edge.plane = plane_id;
            edges.push_back(edge);
        }
    }

    return SUCCESS;
}

int32_t GenerateH2DEdges(
    const RouteData& route_data,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    for (const auto& entry : route_data.entries) {
        if (entry.device_id != phy_id) continue;

        EndpointConfig edge;
        edge.protocol = "ub_ctp";
        edge.comm_id = entry.local_eid;
        edge.placement = "host";
        edge.dst_eid = entry.remote_eid;
        edges.push_back(edge);
    }

    return SUCCESS;
}

int32_t GenerateH2UEdges(
    const RouteData& route_data,
    const std::vector<std::string>& clos_plane_ids,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    edges.clear();

    for (const auto& entry : route_data.entries) {
        if (entry.device_id != phy_id) continue;

        for (const auto& plane_id : clos_plane_ids) {
            EndpointConfig edge;
            edge.protocol = "ub_ctp";
            edge.comm_id = entry.local_eid;
            edge.placement = "host";
            edge.plane = plane_id;
            edges.push_back(edge);
        }
    }

    return SUCCESS;
}

// ============ 核心接口实现 ============

int32_t GenerateLocalCommRes(
    int32_t phy_dev_id,
    const std::map<std::string, std::string>& options,
    LocalCommRes& local_comm_res) {

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

    std::vector<std::string> eid_list;
    int32_t ret = GetEidListByPhyId(phy_dev_id, eid_list);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] Failed to get eid list: " << ret << std::endl;
        return ret;
    }

    if (eid_list.empty()) {
        std::cerr << "[GenerateLocalCommRes] No eid found for phy_dev_id: " << phy_dev_id << std::endl;
        return ERROR_NO_EID_FOUND;
    }

    unsigned int mainboard_id = 0;
    ret = GetMainboardId(phy_dev_id, mainboard_id);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] Failed to get mainboard id: " << ret << std::endl;
    }

    TopoData topo_data;
    ret = ParseTopoFile(topo_path, topo_data);
    if (ret != SUCCESS && ret != ERROR_FILE_NOT_FOUND) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse topo file: " << ret << std::endl;
    }

    RouteData route_data;
    ret = ParseRouteFile(route_path, route_data);
    if (ret != SUCCESS && ret != ERROR_FILE_NOT_FOUND) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse route file: " << ret << std::endl;
    }

    std::vector<std::string> mesh_eids;
    std::string clos_pg_eid;
    std::vector<std::string> clos_plane_ids;

    for (const auto& eid : eid_list) {
        if (IsMeshLayerEid(eid)) {
            mesh_eids.push_back(eid);
        } else if (IsClosLayerEid(eid)) {
            if (clos_pg_eid.empty()) {
                clos_pg_eid = eid;
                clos_plane_ids.push_back("plane_pg_0");
                clos_plane_ids.push_back("plane_pg_1");
            }
        }
    }

    std::vector<EndpointConfig> all_edges;

    std::vector<EndpointConfig> d2d_edges;
    if (!topo_data.links.empty()) {
        GenerateD2DEdges(topo_data, mesh_eids, phy_dev_id, d2d_edges);
        all_edges.insert(all_edges.end(), d2d_edges.begin(), d2d_edges.end());
    }

    std::vector<EndpointConfig> d2u_edges;
    if (!clos_pg_eid.empty() && !topo_data.links.empty()) {
        GenerateD2UEdges(topo_data, clos_pg_eid, clos_plane_ids, phy_dev_id, d2u_edges);
        all_edges.insert(all_edges.end(), d2u_edges.begin(), d2u_edges.end());
    }

    std::vector<EndpointConfig> h2d_edges;
    if (!route_data.entries.empty()) {
        GenerateH2DEdges(route_data, phy_dev_id, h2d_edges);
        all_edges.insert(all_edges.end(), h2d_edges.begin(), h2d_edges.end());
    }

    std::vector<EndpointConfig> h2u_edges;
    if (!clos_plane_ids.empty() && !route_data.entries.empty()) {
        GenerateH2UEdges(route_data, clos_plane_ids, phy_dev_id, h2u_edges);
        all_edges.insert(all_edges.end(), h2u_edges.begin(), h2u_edges.end());
    }

    local_comm_res.version = "1.3";
    local_comm_res.net_instance_id = "";
    local_comm_res.endpoint_list = all_edges;

    return SUCCESS;
}

}  // namespace hixl