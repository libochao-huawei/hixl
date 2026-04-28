/**
 * @file local_comm_res_tool.cc
 * @brief LocalCommRes 生成工具实现文件
 */

#include "local_comm_res_tool.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iomanip>

namespace hixl {

// ============ DCMI 接口封装实现 ============

int32_t GetEidListByPhyId(int32_t phy_dev_id, std::vector<std::string>& eid_list) {
    dcmi_urma_eid_info_t eidList[MAX_EID_NUM];
    size_t eidCnt = MAX_EID_NUM;

    int ret = hal_get_eid_list_by_phy_id(phy_dev_id, eidList, &eidCnt);
    if (ret != 0) {
        std::cerr << "[GetEidListByPhyId] Failed to get eid list, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }

    eid_list.clear();
    for (size_t i = 0; i < eidCnt; ++i) {
        eid_list.push_back(EidToString(eidList[i].eid));
    }

    return SUCCESS;
}

int32_t GetUBEntityList(int32_t phy_dev_id, UEList& ue_list) {
    int ret = HalGetUBEntityList(phy_dev_id, &ue_list);
    if (ret != 0) {
        std::cerr << "[GetUBEntityList] Failed to get UB entity list, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }
    return SUCCESS;
}

int32_t GetMainboardId(int32_t phy_dev_id, unsigned int& mainboard_id) {
    int ret = hal_get_mainboard_id(phy_dev_id, &mainboard_id);
    if (ret != 0) {
        std::cerr << "[GetMainboardId] Failed to get mainboard id, ret=" << ret << std::endl;
        return ERROR_DCMI_INTERFACE_FAILED;
    }
    return SUCCESS;
}

// ============ EID 解析实现 ============

std::string EidToString(const dcmi_urma_eid_t& eid) {
    // 使用 in6.interface_id 作为 EID 的关键部分
    // 格式化为 16 位十六进制字符串
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << eid.in6.interface_id;
    return oss.str();
}

int GetPortFromEid(const std::string& eid) {
    if (eid.length() < 2) {
        return -1;
    }
    // 取最后 2 位
    std::string last = eid.substr(eid.length() - 2);
    int h = std::stoi(last, nullptr, 16);
    int p = ((~128) & h) >> 3;  // 清除最高位，右移 3 位
    return p;
}

int GetServerDieIdFromEid(const std::string& eid) {
    if (eid.length() < 2) {
        return -1;
    }
    // 取倒数第 2 个字符
    char low = eid[eid.length() - 2];
    int h = std::stoi(std::string(1, low), nullptr, 16);
    int die_id = (8 & h) >> 3;  // 提取 bit3
    return die_id;
}

int GetPodDieIdFromEid(const std::string& eid) {
    if (eid.length() < 3) {
        return -1;
    }
    // 取倒数第 3 个字符
    char third = eid[eid.length() - 3];
    int h = std::stoi(std::string(1, third), nullptr, 16);
    int die_id = (4 & h) >> 2;  // 提取 bit2
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
            return eid;  // 第一个 port > 9 的 EID 即为 PG EID
        }
    }
    return "";
}

std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigServer(unsigned int mainboard_id) {
    std::vector<std::tuple<int, int, std::vector<int>>> config;

    // 根据 MAIN_BOARD_ID_* 常量判断服务器类型
    switch (mainboard_id) {
        case MAIN_BOARD_ID_SERVER_TYPE1:  // 0x23
        case MAIN_BOARD_ID_SERVER_8PMESH:  // 0x25
        case MAIN_BOARD_ID_SERVER_16PMESH: // 0x44
        case MAIN_BOARD_ID_SERVER_UBX:     // 0x44
            // 2+4 服务器 (mainboard_id = 35 在原方案中对应此配置)
            if (mainboard_id == 0x23) {  // 假设 0x23 对应 2+4 服务器
                config.push_back(std::make_tuple(0, 3, std::vector<int>{4, 5, 6, 7}));
                config.push_back(std::make_tuple(1, 2, std::vector<int>{5, 6}));
            } else {
                // 其他服务器配置
                config.push_back(std::make_tuple(0, 3, std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
            }
            break;
        default:
            // 默认配置
            config.push_back(std::make_tuple(0, 3, std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
            break;
    }
    return config;
}

std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigPod(unsigned int mainboard_id, int phy_id) {
    std::vector<std::tuple<int, int, std::vector<int>>> config;

    if (mainboard_id == MAIN_BOARD_ID_POD_2D) {  // 0x03
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
    // 解析 topology 文件，提取 link 信息
    // 需要的字段: net_layer, link_type, topo_type, local_a, local_b, remote_a, remote_b
    // 以及 local_a_ports, local_b_ports

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
    // 格式: device_id local_eid remote_eid

    file.close();
    return SUCCESS;
}

// ============ 边生成实现 ============

int32_t GenerateD2DEdges(
    const TopoData& topo_data,
    const std::vector<std::string>& eid_list,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges) {

    (void)eid_list;  // TODO: 实现时可能需要使用 eid_list 进行匹配
    edges.clear();

    // 筛选条件: net_layer = 0, link_type = PEER2PEER, topo_type = 1DMESH
    for (const auto& link : topo_data.links) {
        if (link.net_layer != 0) continue;
        if (link.link_type != "PEER2PEER") continue;
        if (link.topo_type != "1DMESH") continue;
        if (link.local_a != phy_id) continue;

        // 查找 local_a_ports 对应的 EID
        std::string comm_id = link.local_a_ports;
        std::string dst_eid = link.local_b_ports;

        // 如果 port 有效 (1-9)，则添加到边列表
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
        return SUCCESS;  // 没有 CLOS 层 EID
    }

    // 筛选条件: net_layer = 1, link_type = PEER2NET, topo_type = CLOS
    for (const auto& link : topo_data.links) {
        if (link.net_layer != 1) continue;
        if (link.link_type != "PEER2NET") continue;
        if (link.topo_type != "CLOS") continue;
        if (link.local_a != phy_id) continue;

        // CLOS 层使用 pg_eid 作为串口组标识
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

    // 从 route.conf 获取当前 NPU 的 H2D 边
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

    // 从 route.conf 获取 local_eid，结合 CLOS 层 plane_id 生成 H2U 边
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

    // 1. 解析 options
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

    // 2. 获取 EID 列表（通过 DCMI 接口）
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

    // 3. 获取主板 ID
    unsigned int mainboard_id = 0;
    ret = GetMainboardId(phy_dev_id, mainboard_id);
    if (ret != SUCCESS) {
        std::cerr << "[GenerateLocalCommRes] Failed to get mainboard id: " << ret << std::endl;
        // 不作为致命错误，使用默认值继续
    }

    // 4. 解析 topology 文件
    TopoData topo_data;
    ret = ParseTopoFile(topo_path, topo_data);
    if (ret != SUCCESS && ret != ERROR_FILE_NOT_FOUND) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse topo file: " << ret << std::endl;
        return ret;
    }

    // 5. 解析 route.conf 文件
    RouteData route_data;
    ret = ParseRouteFile(route_path, route_data);
    if (ret != SUCCESS && ret != ERROR_FILE_NOT_FOUND) {
        std::cerr << "[GenerateLocalCommRes] Failed to parse route file: " << ret << std::endl;
        return ret;
    }

    // 6. 区分 Mesh 层和 CLOS 层 EID
    std::vector<std::string> mesh_eids;
    std::string clos_pg_eid;
    std::vector<std::string> clos_plane_ids;

    for (const auto& eid : eid_list) {
        if (IsMeshLayerEid(eid)) {
            mesh_eids.push_back(eid);
        } else if (IsClosLayerEid(eid)) {
            // 获取第一个 CLOS 层 EID 作为 PG EID
            if (clos_pg_eid.empty()) {
                clos_pg_eid = eid;
                // 生成 plane_id 列表
                clos_plane_ids.push_back("plane_pg_0");
                clos_plane_ids.push_back("plane_pg_1");
            }
        }
    }

    // 7. 生成各种边
    std::vector<EndpointConfig> all_edges;

    // D2D 边
    std::vector<EndpointConfig> d2d_edges;
    if (!topo_data.links.empty()) {
        GenerateD2DEdges(topo_data, mesh_eids, phy_dev_id, d2d_edges);
        all_edges.insert(all_edges.end(), d2d_edges.begin(), d2d_edges.end());
    }

    // D2U 边
    std::vector<EndpointConfig> d2u_edges;
    if (!clos_pg_eid.empty() && !topo_data.links.empty()) {
        GenerateD2UEdges(topo_data, clos_pg_eid, clos_plane_ids, phy_dev_id, d2u_edges);
        all_edges.insert(all_edges.end(), d2u_edges.begin(), d2u_edges.end());
    }

    // H2D 边
    std::vector<EndpointConfig> h2d_edges;
    if (!route_data.entries.empty()) {
        GenerateH2DEdges(route_data, phy_dev_id, h2d_edges);
        all_edges.insert(all_edges.end(), h2d_edges.begin(), h2d_edges.end());
    }

    // H2U 边
    std::vector<EndpointConfig> h2u_edges;
    if (!clos_plane_ids.empty() && !route_data.entries.empty()) {
        GenerateH2UEdges(route_data, clos_plane_ids, phy_dev_id, h2u_edges);
        all_edges.insert(all_edges.end(), h2u_edges.begin(), h2u_edges.end());
    }

    // 8. 构建输出结构体
    local_comm_res.version = "1.3";
    local_comm_res.net_instance_id = "";  // TODO: 需要确定如何获取
    local_comm_res.endpoint_list = all_edges;

    return SUCCESS;
}

}  // namespace hixl