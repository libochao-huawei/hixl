# LocalCommRes 生成工具接口说明

## 1. 概述

### 1.1 功能说明

`LocalCommRes` 生成工具用于根据当前 NPU 的物理设备 ID，从系统配置文件（topology 文件、route.conf、EID JSON 文件）和 DCMI 接口自动生成本地通信资源信息，输出 `LocalCommRes` 结构体供 HIXL 初始化使用。

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
 *        - "eid_json_path": EID JSON 文件路径（可选，用于从文件加载 EID 列表替代 DCMI 接口调用）
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
options["eid_json_path"] = "/path/to/eid.json";  // 可选

hixl::LocalCommRes localCommRes;
auto ret = hixl::GenerateLocalCommRes(phyId, options, localCommRes);
```

---

### 2.2 DCMI 接口封装

#### 2.2.1 GetUBEntityList

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

### 2.3 文件解析接口

#### 2.3.1 ParseTopoFile

```cpp
namespace hixl {

/**
 * @brief 解析 topology 文件
 * @param [in] topo_path topology 文件路径
 * @param [out] topo_data 解析后的 topology 数据
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t ParseTopoFile(const std::string& topo_path, TopoData& topo_data);

}
```

---

#### 2.3.2 ParseRouteFile

```cpp
namespace hixl {

/**
 * @brief 解析 route.conf 文件
 * @param [in] route_path route.conf 路径
 * @param [out] route_data 解析后的 route 数据
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t ParseRouteFile(const std::string& route_path, RouteData& route_data);

}
```

---

### 2.4 RootInfo 构建接口（rootinfo_builder.h）

> 以下接口定义在 `rootinfo_builder.h` 中，`local_comm_res_tool` 通过该模块获取 EID 和 Port 映射。

#### 2.4.1 ParseEidByte6

```cpp
namespace hixl {

/**
 * @brief 解析 EID 的第6字节
 * @param [in] eid 字符串格式 EID
 * @return EidByte6Info 解析结果，包含 die_id、is_pg_eid、port 等信息
 */
EidByte6Info ParseEidByte6(const std::string& eid);

}
```

---

#### 2.4.2 BuildNpuRootInfo

```cpp
namespace hixl {

/**
 * @brief 根据 NPU ID 构建 RootInfo
 * @param [in] npu_id NPU ID
 * @param [in] is_server 是否为 Server 产品形态
 * @param [out] rootinfo 输出的 RootInfo 结构
 * @param [in] json_path 如果不为空，则从指定 JSON 文件加载 EID 列表（替代 DCMI 接口调用）
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo& rootinfo,
                         const std::string& json_path = "");

}
```

---

#### 2.4.3 GetUrmaDeviceList

```cpp
namespace hixl {

/**
 * @brief 获取 URMA Device 列表
 * @param [in] npu_id NPU ID
 * @param [out] urma_devices URMA Device 列表
 * @param [in] json_path 如果不为空，则从指定 JSON 文件加载（跳过 DCMI 调用）
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice>& urma_devices,
                          const std::string& json_path = "");

}
```

---

#### 2.4.4 GetMeshDieId

```cpp
namespace hixl {

/**
 * @brief 确定 Mesh 层的 die_id
 * @param npu_id NPU ID
 * @param is_server 是否为 Server 产品形态
 * @return Mesh 层所在的 die_id
 *   - Server: Mesh 在 1die
 *   - Pod: 根据 npu_id % 8 判断，0-3 在 0die，4-7 在 1die
 */
int GetMeshDieId(int32_t npu_id, bool is_server);

}
```

---

### 2.5 数据结构

#### 2.5.1 LocalCommRes（输出结构体）

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

#### 2.5.2 EndpointConfig

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

#### 2.5.3 TopoLink

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
    std::vector<std::string> local_a_ports;  // 本地 A 端口列表（串口标识，如 "die_id/port"）
    std::vector<std::string> local_b_ports;  // 本地 B 端口列表
};

}
```

**使用场景**：
- 解析 topology 文件后得到
- 用于 D2D 和 D2U 边生成

---

#### 2.5.4 NpuRootInfo（rootinfo_builder.h）

```cpp
namespace hixl {

/**
 * @brief NPU 串口到 EID 的映射信息
 */
struct NpuRootInfo {
    std::map<std::string, std::string> port_to_eid;  // Mesh 层串口到 EID 映射，key: "die_id/port"
    std::vector<ClosPgEidInfo> clos_pg_eids;          // CLOS 层 PG EID 列表
};

}
```

---

#### 2.5.5 ClosPgEidInfo（rootinfo_builder.h）

```cpp
namespace hixl {

/**
 * @brief CLOS PG EID 信息
 */
struct ClosPgEidInfo {
    std::string eid;       // CLOS PG EID
    int die_id;           // 该 PG EID 对应的 die_id
};

}
```

---

#### 2.5.6 UrmaDevice（rootinfo_builder.h）

```cpp
namespace hixl {

/**
 * @brief URMA Device 结构
 */
struct UrmaDevice {
    std::string name;                      // 设备名称，如 "udma0"
    std::vector<std::string> eid_list;    // 该设备下的所有 EID 列表
};

}
```

---

#### 2.5.7 EidByte6Info（rootinfo_builder.h）

```cpp
namespace hixl {

/**
 * @brief EID 第6字节解析结果
 */
struct EidByte6Info {
    int byte6;           // 原始第6字节值
    int high_nibble;    // 高4位
    int low_nibble;     // 低4位
    int die_id;         // die_id (0 或 1)
    bool is_pg_eid;     // 是否为 PG EID (串口组)
    int port;           // port 值 (0-15)
};

}
```

---

### 2.6 边生成接口

#### 2.6.1 GenerateD2DEdges

```cpp
namespace hixl {

/**
 * @brief 生成 D2D 直连边（Device to Device）
 * @param [in] topo_data topology 解析后的数据
 * @param [in] npu_rootinfos 所有相关 NPU 的 rootinfo 映射
 * @param [in] phy_id 当前 NPU 物理 ID
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 拓扑筛选条件：
 *   - net_layer = 0
 *   - link_type = PEER2PEER
 *   - topo_type = 1DMESH
 *   - local_a 或 local_b 等于 phy_id
 */
int32_t GenerateD2DEdges(
    const TopoData& topo_data,
    const std::map<int32_t, NpuRootInfo>& npu_rootinfos,
    int32_t phy_id,
    std::vector<EndpointConfig>& edges);

}
```

---

#### 2.6.2 GenerateD2UEdges

```cpp
namespace hixl {

/**
 * @brief 生成 D2U 非直连边（Device to UB Gateway）
 * @param [in] plane_pg_0_eid CLOS 层 plane_pg_0 的 EID（可为空）
 * @param [in] plane_pg_1_eid CLOS 层 plane_pg_1 的 EID（可为空）
 * @param [out] d2u_edges 生成的边列表
 */
void GenerateD2UEdges(const std::string& plane_pg_0_eid,
                      const std::string& plane_pg_1_eid,
                      std::vector<EndpointConfig>& d2u_edges);

}
```

---

#### 2.6.3 GenerateH2DEdges

```cpp
namespace hixl {

/**
 * @brief 生成 H2D 直连边（Host to Device）
 * @param [in] route_data route.conf 解析后的数据
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 数据来源：从 route.conf 获取
 *   - comm_id: local_eid
 *   - dst_eid: remote_eid
 *   - placement: host
 */
int32_t GenerateH2DEdges(
    const RouteData& route_data,
    std::vector<EndpointConfig>& edges);

}
```

---

#### 2.6.4 GenerateD2HEdges

```cpp
namespace hixl {

/**
 * @brief 生成 D2H 直连边（Device to Host）
 * @param [in] route_data route.conf 解析后的数据
 * @param [out] edges 生成的边列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 数据来源：从 route.conf 获取
 *   - comm_id: remote_eid
 *   - dst_eid: local_eid
 *   - placement: device
 */
int32_t GenerateD2HEdges(
    const RouteData& route_data,
    std::vector<EndpointConfig>& edges);

}
```

---

#### 2.6.5 GenerateH2UEdges

```cpp
namespace hixl {

/**
 * @brief 生成 H2U 非直连边（Host to UB Gateway）
 * @param [in] plane_pg_0_eid CLOS 层 plane_pg_0 的 EID（可为空）
 * @param [in] plane_pg_1_eid CLOS 层 plane_pg_1 的 EID（可为空）
 * @param [out] h2u_edges 生成的边列表
 */
void GenerateH2UEdges(const std::string& plane_pg_0_eid,
                       const std::string& plane_pg_1_eid,
                       std::vector<EndpointConfig>& h2u_edges);

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
    │ GetMainboardId│              │ ParseRouteFile│              │ ParseTopoFile│
    │ (DCMI 接口)  │               │ (route.conf) │               │ (topo 文件)  │
    └─────────────┘                └─────────────┘                └─────────────┘
           │                               │                               │
           ▼                               ▼                               ▼
    ┌─────────────┐                ┌─────────────┐                ┌─────────────┐
    │ 产品形态判断 │                │ RouteData   │                │  TopoData   │
    │ Pod/Server  │                │ (EID 映射)  │                │ (链路列表)   │
    └─────────────┘                └─────────────┘                └─────────────┘
                                           │                               │
                                           │                               ▼
                                           │                       ┌───────────────┐
                                           │                       │CollectRelated │
                                           │                       │NpuIds         │
                                           │                       └───────────────┘
                                           │                               │
                                           ▼                               ▼
                                           │                       ┌───────────────┐
                                           │                       │BuildNpu       │
                                           │                       │Rootinfos      │
                                           │                       │(DCMI/JSON)    │
                                           │                       └───────────────┘
                                           │                               │
                                           │                               ▼
                                           │                       ┌───────────────┐
                                           │                       │CollectClos    │
                                           │                       │PgEids         │
                                           │                       └───────────────┘
                                           │                               │
           ┌───────────────────────────────┼───────────────────────────────┘
           │                               │                               │
           ▼                               ▼                               ▼
    ┌─────────────┐          ┌─────────────┐          ┌─────────────┐
    │ Generate    │          │ Generate    │          │ Generate    │
    │ D2DEdges    │          │ D2UEdges    │          │ H2DEdges    │
    │ (topo+      │          │ (CLOS PG    │          │ (route)     │
    │  rootinfos) │          │  EIDs)      │          │             │
    └─────────────┘          └─────────────┘          └─────────────┘
                                                               │
           ┌───────────────────────────────────────────────────┘
           │                               │
           ▼                               ▼
    ┌─────────────┐                ┌─────────────┐
    │ Generate    │                │ Generate    │
    │ D2HEdges    │                │ H2UEdges    │
    │ (route)     │                │ (CLOS PG    │
    │             │                │  EIDs)      │
    └─────────────┘                └─────────────┘
           │                               │
           └───────────────┬───────────────┘
                           ▼
                  ┌─────────────────┐
                  │ LocalCommRes    │
                  │ (结构体输出)     │
                  └─────────────────┘
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
| libdcmi.so | DCMI 动态库，通过 dlopen 动态加载，提供 `dcmiv2_init`、`dcmiv2_get_urma_device_cnt`、`dcmiv2_get_eid_list_by_urma_dev_index`、`dcmiv2_get_mainboard_id`、`dcmiv2_get_dev_id_by_chip_phy_id` 等函数 |

### 7.2 内部依赖

| 模块 | 说明 |
|------|------|
| rootinfo_builder.h | RootInfo 构建模块，提供 `BuildNpuRootInfo`、`GetUrmaDeviceList`、`GetMeshDieId`、`ParseEidByte6` 等接口，以及 `NpuRootInfo`、`UrmaDevice`、`EidByte6Info` 等数据结构 |
| local_comm_res_tool.h | 本工具头文件，定义 `LocalCommRes`、`EndpointConfig`、`TopoLink`、`RouteEntry` 等数据结构 |

---

## 8. 遗留事项

| 序号 | 事项 | 负责人 | 备注 |
|------|------|--------|------|
| 1 | **RoCE 边生成** | 待确认 | 当前方案未实现，需确认是否需要实现 |
| 2 | **host_pairs.txt 格式** | 待确认 | 用于 RoCE 边生成，需提供格式说明 |

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