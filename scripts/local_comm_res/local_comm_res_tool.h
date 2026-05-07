/**
 * @file local_comm_res_tool.h
 * @brief LocalCommRes 生成工具头文件
 *
 * 本工具用于在 HIXL 初始化阶段自动生成本地通信资源信息。
 * 支持 Mesh 层和 CLOS 层 EID 和 Port 的获取。
 *
 * DCMI 接口通过 dlopen 方式动态加载 libdcmi.so，避免直接依赖 hal.h
 */

#ifndef HIXL_LOCAL_COMM_RES_TOOL_H_
#define HIXL_LOCAL_COMM_RES_TOOL_H_

#include <string>
#include <vector>
#include <map>
#include <tuple>

namespace hixl {

/**
 * @brief 错误码定义
 */
const int32_t SUCCESS = 0;
const int32_t ERROR_INVALID_PARAM = 1001;
const int32_t ERROR_FILE_NOT_FOUND = 1002;
const int32_t ERROR_FILE_PARSE_FAILED = 1003;
const int32_t ERROR_DCMI_INTERFACE_FAILED = 1004;
const int32_t ERROR_NO_EID_FOUND = 1005;
const int32_t ERROR_NO_CLOS_EID_FOUND = 1006;

// ============ DCMI 相关数据结构（从 hal.h 复制，避免直接引用） ============

const int32_t DCMI_URMA_EID_SIZE = 16;
const int32_t MAX_EID_NUM = 32;
const int32_t MAX_NPU_COUNT = 64;
const int32_t MAX_EID_PER_UE = 32;
const int32_t MAX_UE_PER_NPU = 8;

/**
 * @brief DCMI URMA EID 结构
 */
typedef union dcmi_urma_eid {
    unsigned char raw[DCMI_URMA_EID_SIZE];
    struct {
        unsigned long subnet_prefix;
        unsigned long interface_id;
    } in6;
} dcmi_urma_eid_t;

/**
 * @brief DCMI URMA EID 信息结构
 */
typedef struct dcmi_urma_eid_info {
    dcmi_urma_eid_t eid;
    unsigned int eid_index;
} dcmi_urma_eid_info_t;

/**
 * @brief UB 实体结构
 */
typedef struct {
    dcmi_urma_eid_info_t eidList[MAX_EID_PER_UE];
    unsigned int eidNum;
} UBEntity;

/**
 * @brief UE 列表结构
 */
typedef struct {
    UBEntity ueList[MAX_UE_PER_NPU];
    unsigned int ueNum;
} UEList;

// ============ 端点配置结构 ============

/**
 * @brief 端点配置结构
 */
struct EndpointConfig {
    std::string protocol;       // "ub_ctp", "ub_tp", "roce"
    std::string comm_id;        // EID 地址
    std::string placement;      // "device" 或 "host"
    std::string plane;          // plane_id（可选）
    std::string dst_eid;        // 目标 EID（用于直连场景）
};

/**
 * @brief LocalCommRes 结构体（替代 JSON 输出）
 */
struct LocalCommRes {
    std::string version;                        // 版本号，默认 "1.3"
    std::string net_instance_id;                // 网络实例 ID
    std::vector<EndpointConfig> endpoint_list;  // 端点列表
};

// ============ Topology 数据结构 ============

/**
 * @brief Topology 链路结构
 */
struct TopoLink {
    int32_t net_layer;          // 网络层级 (0: Mesh, 1: CLOS)
    std::string link_type;     // 连接类型: PEER2PEER, PEER2NET
    std::string topo_type;     // 拓扑类型: 1DMESH, CLOS
    int32_t local_a;          // 本地节点 A
    int32_t local_b;          // 本地节点 B
    int32_t remote_a;         // 远端节点 A
    int32_t remote_b;         // 远端节点 B
    std::vector<std::string> local_a_ports;  // 本地 A 端口列表（串口标识，如 "die_id/port"）
    std::vector<std::string> local_b_ports;  // 本地 B 端口列表
};

struct TopoData {
    std::vector<TopoLink> links;
};

// ============ Route 数据结构 ============

/**
 * @brief Route 条目结构
 */
struct RouteEntry {
    int32_t device_id;          // 设备 ID
    std::string local_eid;     // 本地 EID
    std::string remote_eid;    // 远端 EID
};

struct RouteData {
    std::vector<RouteEntry> entries;
};

// ============ URMA Device 数据结构 ============

/**
 * @brief URMA Device 结构（参考 Python 的 UrmaDevice 类）
 * 每个 URMA Device 包含多个 EID，die_id 从第一个 EID 获取
 */
struct UrmaDevice {
    std::string name;                      // 设备名称，如 "udma0"
    std::vector<std::string> eid_list;    // 该设备下的所有 EID 列表
};

/**
 * @brief 获取 URMA Device 列表
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] urma_devices URMA Device 列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetUrmaDeviceList(int32_t phy_dev_id, std::vector<UrmaDevice>& urma_devices);

// ============ RootInfo 数据结构 ============

/**
 * @brief NPU 串口到 EID 的映射信息
 * key: 串口标识 "die_id/port"
 * value: 对应的 EID 字符串
 */
struct NpuRootInfo {
    std::map<std::string, std::string> port_to_eid;  // Mesh 层串口到 EID 映射
    std::string clos_pg_eid;                          // CLOS 层 PG EID（串口组标识）
};

// ============ 核心接口 ============

/**
 * @brief 生成 LocalCommRes 结构体
 * @param [in] phy_dev_id 物理设备 ID，通过 aclrtGetPhyDevIdByLogicDevId 获取
 * @param [in] options 生成选项，包含输入文件路径等
 *        - "topo_path": topology 文件路径
 *        - "route_path": route.conf 路径
 * @param [out] local_comm_res 输出的 LocalCommRes 结构体
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateLocalCommRes(
    int32_t phy_dev_id,
    const std::map<std::string, std::string>& options,
    LocalCommRes& local_comm_res
);

// ============ DCMI 接口封装 ============

/**
 * @brief 通过 phyId 获取 EID 列表
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] eid_list EID 列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetEidListByPhyId(int32_t phy_dev_id, std::vector<std::string>& eid_list);

/**
 * @brief 获取 UB 实体列表
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] ue_list UB 实体列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetUBEntityList(int32_t phy_dev_id, UEList& ue_list);

/**
 * @brief 获取主板 ID
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] mainboard_id 主板 ID
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetMainboardId(int32_t phy_dev_id, unsigned int& mainboard_id);

// ============ EID 解析接口 ============

/**
 * @brief 将 dcmi_urma_eid_t 转换为字符串格式
 * @param [in] eid DCMI EID 结构
 * @return 字符串格式的 EID
 */
std::string EidToString(const dcmi_urma_eid_t& eid);

/**
 * @brief 从 EID 获取 port（单个物理串口）
 * @param [in] eid 字符串格式 EID
 * @return port 编号 (0-9)，port > 9 表示 CLOS 层串口组
 */
int GetPortFromEid(const std::string& eid);

/**
 * @brief 从 EID 获取 die_id（Server 类型）
 * @param [in] eid 字符串格式 EID
 * @return die_id (0 或 1)
 */
int GetServerDieIdFromEid(const std::string& eid);

/**
 * @brief 从 EID 获取 die_id（Pod 类型）
 * @param [in] eid 字符串格式 EID
 * @return die_id (0 或 1)
 */
int GetPodDieIdFromEid(const std::string& eid);

/**
 * @brief 判断 EID 是否为 Mesh 层（单个物理串口）
 * @param [in] eid 字符串格式 EID
 * @return true: Mesh 层 (port ≤ 9), false: CLOS 层 (port > 9)
 */
bool IsMeshLayerEid(const std::string& eid);

/**
 * @brief 判断 EID 是否为 CLOS 层（串口组）
 * @param [in] eid 字符串格式 EID
 * @return true: CLOS 层 (port > 9), false: Mesh 层 (port ≤ 9)
 */
bool IsClosLayerEid(const std::string& eid);

// ============ CLOS 层专用接口 ============

/**
 * @brief 从 eid_list 获取 CLOS 层 EID（PG EID，串口组标识）
 * @param [in] eid_list EID 列表
 * @return 第一个 port > 9 的 EID，若未找到则返回空字符串
 */
std::string GetPgEid(const std::vector<std::string>& eid_list);

/**
 * @brief 获取 CLOS 层串口组配置（Server 类型）
 * @param [in] mainboard_id 主板 ID
 * @return 配置列表: [(die_id, fe_id, [port1, port2, ...]), ...]
 */
std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigServer(unsigned int mainboard_id);

/**
 * @brief 获取 CLOS 层串口组配置（Pod 类型）
 * @param [in] mainboard_id 主板 ID
 * @param [in] phy_id 物理设备 ID
 * @return 配置列表: [(die_id, fe_id, [port1, port2, ...]), ...]
 */
std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigPod(unsigned int mainboard_id, int phy_id);

// ============ 文件解析接口 ============

/**
 * @brief 解析 topology 文件
 * @param [in] topo_path topology 文件路径
 * @param [out] topo_data 解析后的 topology 数据
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t ParseTopoFile(const std::string& topo_path, TopoData& topo_data);

/**
 * @brief 解析 route.conf 文件
 * @param [in] route_path route.conf 文件路径
 * @param [out] route_data 解析后的 route 数据
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t ParseRouteFile(const std::string& route_path, RouteData& route_data);

// ============ 边生成接口 ============

/**
 * @brief 生成 D2D 直连边（Device to Device）
 * @param [in] topo_data topology 数据
 * @param [in] npu_rootinfos 所有相关 NPU 的 rootinfo 映射
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateD2DEdges(
    const TopoData& topo_data,
    const std::map<int32_t, NpuRootInfo>& npu_rootinfos,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges);

/**
 * @brief 生成 D2U 非直连边（Device to UB Gateway）
 * @param [in] topo_data topology 数据
 * @param [in] npu_rootinfos 所有相关 NPU 的 rootinfo 映射
 * @param [in] clos_plane_ids CLOS 层 plane_id 列表
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateD2UEdges(
    const TopoData& topo_data,
    const std::map<int32_t, NpuRootInfo>& npu_rootinfos,
    const std::vector<std::string>& clos_plane_ids,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges);

/**
 * @brief 生成 H2D 直连边（Host to Device）
 * @param [in] route_data route 数据
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateH2DEdges(
    const RouteData& route_data,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges);

/**
 * @brief 生成 H2U 非直连边（Host to UB Gateway）
 * @param [in] route_data route 数据
 * @param [in] clos_plane_ids CLOS 层 plane_id 列表
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GenerateH2UEdges(
    const RouteData& route_data,
    const std::vector<std::string>& clos_plane_ids,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges);

}  // namespace hixl

#endif  // HIXL_LOCAL_COMM_RES_TOOL_H_