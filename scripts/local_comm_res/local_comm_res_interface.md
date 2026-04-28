# LocalCommRes 生成工具接口说明

## 1. 概述

### 1.1 功能说明

`LocalCommRes` 生成工具用于在 HIXL 初始化阶段自动检测用户输入的 `endpoint_list`，如果为空或不存在，则自动从系统配置文件中读取并生成本地通信资源信息。

### 1.2 主要能力

| 能力 | 说明 |
|------|------|
| **Mesh 层 EID/PORT 获取** | 从 DCMI 接口获取 urma_dev_list，解析 eid_list，提取 port ≤ 9 的 EID 对应单个物理串口 |
| **CLOS 层 EID/PORT 获取** | 从 DCMI 接口获取 urma_dev_list，解析 eid_list，提取 port > 9 的 EID 作为串口组标识（PG EID） |
| **D2D 边生成** | 基于 Mesh 层 EID 生成设备间直连边 |
| **D2U 边生成** | 基于 CLOS 层串口组生成设备到 UB Gateway 的非直连边 |
| **H2D 边生成** | 从 route.conf 获取本地 EID 和远端 EID 生成主机到设备边 |
| **H2U 边生成** | 从 route.conf 获取本地 EID，结合 CLOS 层 plane_id 生成主机到 UB Gateway 边 |
| **RoCE 边生成** | 暂不实现（见第 8 节遗留事项） |

---

## 2. 接口列表

### 2.1 核心接口

#### 2.1.1 GenerateLocalCommRes

```cpp
namespace hixl {

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

}
```

**调用示例**：
```cpp
int32_t logicId = 0;
int32_t phyId = 0;
aclrtGetDevice(&logicId);
aclrtGetPhyDevIdByLogicDevId(logicId, &phyId);

std::map<std::string, std::string> options;
options["topo_path"] = "/etc/superpod_2d_noroce.json";
options["route_path"] = "/lib/route.conf";

hixl::LocalCommRes localCommRes;
auto ret = hixl::GenerateLocalCommRes(phyId, options, localCommRes);
```

---

### 2.2 DCMI 接口封装

#### 2.2.1 GetEidListByPhyId

```cpp
namespace hixl {

/**
 * @brief 通过 phyId 获取 EID 列表
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] eid_list EID 列表（字符串格式）
 * @return 成功: SUCCESS, 失败: 其它错误码
 * @note 底层调用 hal_get_eid_list_by_phy_id
 */
int32_t GetEidListByPhyId(int32_t phy_dev_id, std::vector<std::string>& eid_list);

}
```

**使用场景**：
- DCMI 接口获取 EID 列表的第一步
- 返回的 EID 为字符串格式，可直接用于解析

---

#### 2.2.2 GetUBEntityList

```cpp
namespace hixl {

/**
 * @brief 获取 UB 实体列表
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] ue_list UB 实体列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 * @note 底层调用 HalGetUBEntityList
 */
int32_t GetUBEntityList(int32_t phy_dev_id, UEList& ue_list);

}
```

---

#### 2.2.3 GetMainboardId

```cpp
namespace hixl {

/**
 * @brief 获取主板 ID
 * @param [in] phy_dev_id 物理设备 ID
 * @param [out] mainboard_id 主板 ID
 * @return 成功: SUCCESS, 失败: 其它错误码
 * @note 用于确定 CLOS 层串口组配置
 */
int32_t GetMainboardId(int32_t phy_dev_id, unsigned int& mainboard_id);

}
```

---

#### 2.2.4 EidToString

```cpp
namespace hixl {

/**
 * @brief 将 dcmi_urma_eid_t 转换为字符串格式
 * @param [in] eid DCMI EID 结构
 * @return 字符串格式的 EID
 */
std::string EidToString(const dcmi_urma_eid_t& eid);

}
```

---

### 2.3 EID 解析接口

#### 2.3.1 GetPortFromEid

```cpp
namespace hixl {

/**
 * @brief 从 EID 获取 port（单个物理串口）
 * @param [in] eid 字符串格式 EID
 * @return port 编号 (0-9)
 *         - port ≤ 9: Mesh 层单个物理串口
 *         - port > 9: CLOS 层串口组（应使用 GetPgEid）
 * @note 算法: 取 EID 最后 2 位十六进制字符，清除 bit7，右移 3 位
 */
int GetPortFromEid(const std::string& eid);

}
```

**使用场景**：
- 遍历 eid_list 时，判断该 EID 是 Mesh 层还是 CLOS 层
- port ≤ 9 表示单个物理串口，用于 D2D 边生成

---

#### 2.2.2 GetServerDieId

```cpp
namespace hixl {

/**
 * @brief 从 EID 获取 die_id（Server 类型）
 * @param [in] eid EID 字符串
 * @return die_id (0 或 1)
 * @note 算法: 取 EID 倒数第 2 个字符，提取 bit3，右移 3 位
 */
int GetServerDieId(const std::string& eid);

}
```

---

#### 2.2.3 GetPodDieId

```cpp
namespace hixl {

/**
 * @brief 从 EID 获取 die_id（Pod 类型）
 * @param [in] eid EID 字符串
 * @return die_id (0 或 1)
 * @note 算法: 取 EID 倒数第 3 个字符，提取 bit2，右移 2 位
 */
int GetPodDieId(const std::string& eid);

}
```

---

#### 2.2.4 IsMeshLayerEid

```cpp
namespace hixl {

/**
 * @brief 判断 EID 是否为 Mesh 层（单个物理串口）
 * @param [in] eid EID 字符串
 * @return true: Mesh 层 (port ≤ 9), false: CLOS 层 (port > 9)
 */
bool IsMeshLayerEid(const std::string& eid);

}
```

---

#### 2.2.5 IsClosLayerEid

```cpp
namespace hixl {

/**
 * @brief 判断 EID 是否为 CLOS 层（串口组）
 * @param [in] eid EID 字符串
 * @return true: CLOS 层 (port > 9), false: Mesh 层 (port ≤ 9)
 */
bool IsClosLayerEid(const std::string& eid);

}
```

---

### 2.3 CLOS 层专用接口

#### 2.3.1 GetPgEid

```cpp
namespace hixl {

/**
 * @brief 从 urma_dev 获取 CLOS 层 EID（PG EID，串口组标识）
 * @param [in] eid_list EID 列表
 * @return 第一个 port > 9 的 EID，若未找到则返回空字符串
 * @note CLOS 层的 EID 代表一组串口的聚合，使用 port > 9 区分
 */
std::string GetPgEid(const std::vector<std::string>& eid_list);

}
```

---

#### 2.3.2 GetLevel1ConfigServer

```cpp
namespace hixl {

/**
 * @brief 获取 CLOS 层串口组配置（Server 类型）
 * @param [in] mainboard_id 主板 ID
 *        - 35: 2+4 服务器
 *        - 37, 39: 其它服务器
 * @return 配置列表，格式: [(die_id, fe_id, [port1, port2, ...]), ...]
 *
 * 配置示例:
 *   - mainboard_id = 35: {{0, 3, {4, 5, 6, 7}}, {1, 2, {5, 6}}}
 *   - mainboard_id = 37/39: {{0, 3, {1, 2, 3, 4, 5, 6, 7, 8}}}
 */
std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigServer(int mainboard_id);

}
```

---

#### 2.3.3 GetLevel1ConfigPod

```cpp
namespace hixl {

/**
 * @brief 获取 CLOS 层串口组配置（Pod 类型）
 * @param [in] mainboard_id 主板 ID
 * @param [in] phy_id 物理设备 ID
 * @return 配置列表，格式: [(die_id, fe_id, [port1, port2, ...]), ...]
 *
 * 配置示例 (mainboard_id = 3, phy_id % 8 < 4):
 *   {{0, 2, {1, 2}}, {1, 2, {0, 1, 2, 3, 5, 6}}}
 */
std::vector<std::tuple<int, int, std::vector<int>>> GetLevel1ConfigPod(int mainboard_id, int phy_id);

}
```

---

### 2.4 数据结构

#### 2.4.1 LocalCommRes（输出结构体）

```cpp
namespace hixl {

/**
 * @brief LocalCommRes 结构体（替代 JSON 输出）
 * 本工具的最终输出，直接传递给使用 local comm res 的函数
 */
struct LocalCommRes {
    std::string version;                        // 版本号，默认 "1.3"
    std::string net_instance_id;                // 网络实例 ID
    std::vector<EndpointConfig> endpoint_list;  // 端点列表
};

}
```

---

#### 2.4.2 EndpointConfig

```cpp
namespace hixl {

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

}
```

---

#### 2.4.3 TopoLink

```cpp
namespace hixl {

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
    std::string local_a_ports;  // 本地 A 端口（EID）
    std::string local_b_ports;  // 本地 B 端口（EID）
};

}
```

**使用场景**：
- 解析 topology 文件后得到
- 用于 D2D 和 D2U 边生成
```

---

### 2.5 边生成接口

#### 2.5.1 GenerateD2DEdges

```cpp
namespace hixl {

/**
 * @brief 生成 D2D 直连边（Device to Device）
 * @param [in] topo_data topology 解析后的数据
 * @param [in] npu_eid_ports NPU 的 EID 和 Port 映射
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 拓扑筛选条件：
 *   - net_layer = 0
 *   - link_type = PEER2PEER
 *   - topo_type = 1DMESH
 *   - local_a 在 NPU ID 范围内
 */
Status GenerateD2DEdges(
    const TopoData& topo_data,
    const NpuEidPortWithClos& npu_eid_ports,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges
);

}
```

---

#### 2.5.2 GenerateD2UEdges

```cpp
namespace hixl {

/**
 * @brief 生成 D2U 非直连边（Device to UB Gateway）
 * @param [in] topo_data topology 解析后的数据
 * @param [in] npu_eid_ports NPU 的 EID 和 Port 映射（包含 CLOS 层）
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 拓扑筛选条件：
 *   - net_layer = 1
 *   - link_type = PEER2NET
 *   - topo_type = CLOS
 *   - local_a 在 NPU ID 范围内
 *
 * @note 使用 CLOS 层的 pg_eid 和串口组信息生成边
 */
Status GenerateD2UEdges(
    const TopoData& topo_data,
    const NpuEidPortWithClos& npu_eid_ports,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges
);

}
```

---

#### 2.5.3 GenerateH2DEdges

```cpp
namespace hixl {

/**
 * @brief 生成 H2D 直连边（Host to Device）
 * @param [in] route_data route.conf 解析后的数据
 * @param [in] npu_eid_ports NPU 的 EID 和 Port 映射
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 数据来源：从 route.conf 获取
 *   - comm_id: local_eid
 *   - dst_eid: remote_eid
 *   - placement: host
 */
Status GenerateH2DEdges(
    const RouteData& route_data,
    const NpuEidPortWithClos& npu_eid_ports,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges
);

}
```

---

#### 2.5.4 GenerateH2UEdges

```cpp
namespace hixl {

/**
 * @brief 生成 H2U 非直连边（Host to UB Gateway）
 * @param [in] route_data route.conf 解析后的数据
 * @param [in] npu_eid_ports NPU 的 EID 和 Port 映射（包含 CLOS 层）
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 数据来源：
 *   - comm_id: 从 route.conf 获取 local_eid
 *   - plane: 使用 CLOS 层的 plane_id (plane_pg_0 或 plane_pg_1)
 *   - placement: host
 */
Status GenerateH2UEdges(
    const RouteData& route_data,
    const NpuEidPortWithClos& npu_eid_ports,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges
);

}
```

---

## 3. 数据流图

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                   GenerateLocalCommRes                   │
                    └─────────────────────────────────────────────────────────┘
                                           │
           ┌───────────────────────────────┼───────────────────────────────┐
           │                               │                               │
           ▼                               ▼                               ▼
    ┌─────────────┐                ┌─────────────┐                ┌─────────────┐
    │ DCMI 接口   │                │ route.conf  │                │ topology    │
    │ 获取        │                │ 读取         │                │ 文件读取     │
    │ urma_dev    │                │             │                │             │
    └─────────────┘                └─────────────┘                └─────────────┘
           │                               │                               │
           ▼                               │                               ▼
    ┌─────────────────────────────────────────────────────────────────────────┐
    │                     解析 EID list                                        │
    │  ┌─────────────────────────┐    ┌─────────────────────────┐            │
    │  │ Mesh 层 (port ≤ 9)      │    │ CLOS 层 (port > 9)       │            │
    │  │ - GetMeshPort           │    │ - GetPgEid              │            │
    │  │ - GetServerDieId        │    │ - GetLevel1ConfigServer │            │
    │  │ - GetPodDieId            │    │ - GetLevel1ConfigPod    │            │
    │  └─────────────────────────┘    └─────────────────────────┘            │
    └─────────────────────────────────────────────────────────────────────────┘
                   │                                        │
                   ▼                                        ▼
           ┌───────────────┐                        ┌───────────────┐
           │ NpuMeshEidPort │                        │  ClosEidPort   │
           └───────────────┘                        └───────────────┘
                   │                                        │
                   └────────────────┬───────────────────────┘
                                    ▼
                         ┌─────────────────────┐
                         │ NpuEidPortWithClos   │
                         └─────────────────────┘
                                    │
           ┌─────────────────────────┼─────────────────────────┐
           │                         │                         │
           ▼                         ▼                         ▼
    ┌─────────────┐          ┌─────────────┐          ┌─────────────┐
    │ Generate    │          │ Generate    │          │ Generate    │
    │ D2DEdges    │          │ D2UEdges    │          │ H2DEdges    │
    │ (Mesh)      │          │ (CLOS)      │          │             │
    └─────────────┘          └─────────────┘          └─────────────┘
                                                           │
                                                           ▼
                                                    ┌─────────────┐
                                                    │ Generate    │
                                                    │ H2UEdges    │
                                                    └─────────────┘
                                                           │
                                                           ▼
                                                    ┌─────────────┐
                                                    │ JSON 输出    │
                                                    │ (LocalCommRes)│
                                                    └─────────────┘
```

---

## 4. 输出格式

生成工具输出的 `LocalCommRes` 结构体如下：

```cpp
struct LocalCommRes {
    std::string version;                        // 版本号，默认 "1.3"
    std::string net_instance_id;                // 网络实例 ID
    std::vector<EndpointConfig> endpoint_list;  // 端点列表
};

struct EndpointConfig {
    std::string protocol;       // "ub_ctp", "ub_tp", "roce"
    std::string comm_id;        // EID 地址
    std::string placement;      // "device" 或 "host"
    std::string plane;          // plane_id（可选）
    std::string dst_eid;        // 目标 EID（用于直连场景）
};
```

**注意**：`net_instance_id` 字段暂未赋值，需要后续确定如何获取。

---

## 5. 错误码说明

| 错误码 | 说明 |
|--------|------|
| SUCCESS (0) | 成功 |
| ERROR_INVALID_PARAM | 无效参数 |
| ERROR_FILE_NOT_FOUND | 配置文件不存在 |
| ERROR_FILE_PARSE_FAILED | 配置文件解析失败 |
| ERROR_DCMI_INTERFACE_FAILED | DCMI 接口调用失败 |
| ERROR_NO_EID_FOUND | 未找到有效的 EID |
| ERROR_NO_CLOS_EID_FOUND | 未找到 CLOS 层 EID（对于需要 CLOS 层的场景） |

---

## 6. 使用示例

```cpp
#include "local_comm_res_tool.h"

int main() {
    // 1. 获取当前设备信息
    int32_t logicId = 0;
    int32_t phyId = 0;
    aclrtGetDevice(&logicId);
    aclrtGetPhyDevIdByLogicDevId(logicId, &phyId);

    // 2. 设置生成选项
    std::map<std::string, std::string> options;
    options["topo_path"] = "/etc/superpod_2d_noroce.json";
    options["route_path"] = "/lib/route.conf";

    // 3. 调用生成接口
    hixl::LocalCommRes localCommRes;
    auto ret = hixl::GenerateLocalCommRes(phyId, options, localCommRes);
    if (ret != hixl::SUCCESS) {
        printf("GenerateLocalCommRes failed: %d\n", ret);
        return ret;
    }

    // 4. 使用生成的 localCommRes
    printf("Generated LocalCommRes version: %s\n", localCommRes.version.c_str());
    printf("Endpoint count: %zu\n", localCommRes.endpoint_list.size());

    return 0;
}
```

---

## 7. 依赖说明

### 7.1 外部依赖

| 依赖 | 说明 |
|------|------|
| hal.h / hal.c | DCMI 接口封装，提供 `hal_get_eid_list_by_phy_id`、`HalGetUBEntityList`、`hal_get_mainboard_id` 等函数 |
| ACL Runtime | 用于获取设备信息的 ACL 接口（`aclrtGetDevice`、`aclrtGetPhyDevIdByLogicDevId`） |
| libdcmi.so | DCMI 动态库，通过 dlopen 加载 |

### 7.2 内部依赖

| 模块 | 说明 |
|------|------|
| hixl_inner_types.h | EndpointConfig 等数据结构定义 |
| hixl_types.h | HIXL 类型定义 |

### 7.3 编译说明

编译时需要包含 hal.h 所在路径，并链接 libdcmi.so：

```bash
g++ -I/path/to/hal.h -ldcmi local_comm_res_tool.cc -o local_comm_res_tool
```

---

## 8. 遗留事项

| 序号 | 事项 | 负责人 | 备注 |
|------|------|--------|------|
| 1 | **RoCE 边生成** | 待确认 | 当前方案未实现，需确认是否需要实现 |
| 2 | **host_pairs.txt 格式** | 待确认 | 用于 RoCE 边生成，需提供格式说明 |
| 3 | **DCMI 接口确认** | 待确认 | 需确认 urma_dev_list 获取方式是否正确 |
| 4 | **mainboard_id 获取方式** | 待确认 | 当前通过 options 传入，是否有其他方式获取 |
| 5 | **CLOSE 层串口组配置表** | 待确认 | 配置表是否完整，是否需要动态查询 |
| 6 | **拓扑筛选逻辑** | 待确认 | D2D/D2U 边的拓扑筛选条件是否正确 |

---

## 9. 版本历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|----------|
| 1.0 | 2026-04-27 | - | 初始版本 |

---

## 10. 审阅签字

| 角色 | 姓名 | 日期 | 签字 |
|------|------|------|------|
| 开发者 | | | |
| 审阅者 | | | |