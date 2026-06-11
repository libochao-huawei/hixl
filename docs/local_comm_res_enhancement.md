# LocalCommRes 模块增强需求文档

## 一、需求概述

本次需求包含三个独立改动点，分别针对 topo 文件路径、route.conf fallback 机制、以及 H2U 边 comm_id 获取逻辑进行优化或修复。

| 改动点 | 优先级 | 影响范围 |
|--------|--------|---------|
| 1. Topo 文件路径调整 | 高 | BuildEndpointListFromOptions |
| 2. route.conf 不存在时的 procfs fallback | 高 | GenerateLocalCommRes |
| 3. H2U 边 comm_id 纠错 | 高 | GenerateH2UEdges / CollectAllEdges |

---

## 二、需求详述

### 改动点 1：Topo 文件路径调整

#### 现状

`FindLatestTopoFile()` 在 `/etc/` 目录下模糊匹配以 `noroce.json` 结尾的文件，返回修改时间最新的一个。

#### 目标

改用固定路径 `/usr/local/Ascend/driver/topo/950/`，根据 DCMI 获取的 `mainboard_id` 匹配产品形态，选择对应的 topo 文件。

#### 产品形态与文件映射（待确认）

根据环境 `/usr/local/Ascend/driver/topo/950/` 下的文件列表：

| 文件名 | 推测产品形态 | mainboard_id 范围    |
|--------|-------------|--------------------|
| `atlas_350_1.json` | Atlas 350 1 | 不涉及                |
| `atlas_350_2.json` | Atlas 350 2 | 不涉及                |
| `atlas_350_3.json` | Atlas 350 3 | 不涉及                |
| `atlas_850_1.json` | Atlas 850 1 | 0x21-0x46 (Server) |
| `atlas_850_2.json` | Atlas 850 2 | -                  |
| `atlas_850_3.json` | Atlas 850 3 | -                  |
| `atlas_950_1.json` | Atlas 950 | 0x3;0x5;0x7 (pod)  |

#### 实现方案

**新增函数**（建议放在 `local_comm_res_generator_v1.cc`）：

```cpp
// 根据 mainboard_id 判断产品形态
bool MatchProductForm(uint32_t mainboard_id, const std::string &topo_filename);

// 在指定目录下查找匹配的 topo 文件
std::string FindTopoFileByMainboardId(const std::string &topo_dir, uint32_t mainboard_id);
```

**实现逻辑**：

```
FindTopoFileByMainboardId(topo_dir, mainboard_id):
  1. 判断 mainboard_id 对应的产品形态（Pod1/Pod2/Pod3/Server）
  2. 遍历 topo_dir 下的所有 .json 文件
  3. 用文件名匹配产品形态后缀（如 atlas_850_2.json 中的 850_2）
  4. 返回匹配的文件路径，找不到则返回空

MatchProductForm(mainboard_id, topo_filename):
  - Pod 产品（0x3/0x5/0x7）：匹配 atlas_350_{1,2,3}.json
  - Server 产品（0x21-0x46）：匹配 atlas_850_*.json 或 atlas_950_*.json
```

**修改点**：

- 修改 `FindLatestTopoFile()` 逻辑或新增 `FindTopoFileByMainboardId()`
- `GenerateLocalCommRes` 中调用 `GetMainboardId` 后，传入 `mainboard_id` 选择正确的 topo 文件

**常量建议**：

```cpp
constexpr const char *kDefaultTopoDir = "/usr/local/Ascend/driver/topo/950/";
```

---

### 改动点 2：route.conf 不存在时的 procfs fallback

#### 现状

`ParseRouteFile` 当 `/lib/route.conf` 不存在时返回 `PARAM_INVALID`，导致 `GenerateLocalCommRes` 失败。

#### 目标

当 `/lib/route.conf` 不存在时，通过 procfs 接口（`/proc/ascend_ub/dev_id` + `pair_info`）动态获取路由信息，生成 `RouteData`。

#### procfs 接口说明

```
/proc/ascend_ub/        （或 /proc/asdrv_ub/）
  ├── dev_id     : 写入 NPU ID，选择查询的设备
  └── pair_info  : 读取，返回该设备的配对信息
```

**pair_info 格式示例**：

```
-------------------show dev pair info-------------------
Dev id info. (dev_id=0;slot_id=0;module_id=0;chan_num=1)
Eid info. (bus instance eid=0000:0000:0000:0000:0000:0000:0001:0030; d2d eid=0000:0000:007f:0500:0010:0000:df00:1b00)
chan_id=0;flag=1;hop=1;rsv=0
local_eid: 0000:0000:007f:0200:0010:0000:df00:9001
remote_eid: 0000:0000:0000:0200:0010:0000:df00:0101
---------------------------end--------------------------
```

#### 实现方案

**核心函数**（已实现，需调试）：

```cpp
int32_t GenerateRouteDataFromProcfs(const std::set<int32_t> &related_npu_ids,
                                    RouteData &route_data);
```

**辅助函数**：

| 函数 | 功能 |
|------|------|
| `FindProcBasePath()` | 查找 `/proc/ascend_ub` 或 `/proc/asdrv_ub` |
| `ParsePairInfoForDevice(content, npu_id, slot_id, local_eid, remote_eid)` | 解析 pair_info 文本 |
| `IsFileExists(path)` | 检查文件是否存在 |

**调用链**：

```
GenerateLocalCommRes
  ├─ ParseRouteFile(route_path, route_data)
  │    └─ 文件不存在 → 返回 PARAM_INVALID
  │         └─ GenerateRouteDataFromProcfs(related_npu_ids, route_data)
  │              └─ 遍历 8 个 NPU：
  │                   1. 写入 dev_id 文件
  │                   2. 读取 pair_info
  │                   3. 解析 slot_id、local_eid、remote_eid
  │                   4. 生成 RouteEntry（D2H 和 H2D 各一条）
  └─ 后续流程不变
```

**EID 格式说明**：

- pair_info 中的 EID 格式：`0000:0000:007f:0200:0010:0000:df00:9001`（冒号分隔）
- 需要去掉所有冒号，转换为：`00000000007f020000100000df009001`
- `FormatEid()` 函数负责此转换

#### 注意事项

- procfs 写入需要 root 权限，普通用户可能无法访问
- 可考虑将此 fallback 设为可选项，通过配置开关控制

---

### 改动点 3：H2U 边 comm_id 纠错

#### 现状

当前 `GenerateH2UEdges` 使用的是**与 Host 直连的 Device 的 PG EID** 作为 H2U 边的 `comm_id`。

#### 问题

这会导致 H2rD、H2rH 场景建链错误。正确的做法是使用 **Host 自身的 PG EID**。

#### 目标

通过执行 `urma_admin show` 命令获取 Host 的 URMA 组信息，解析后选择具有 **8 个物理串口**的 URMA 组的 PG EID 作为 Host 的 PG EID。

#### 实现方式

通过 `popen("urma_admin show", "r")` 执行命令，解析输出结果。

#### urma_admin show 输出格式

```
num ubep_dev tp_type eid link

---

0 udma10 UB eid0 0000:0000:007f:0400:0010:0000:df00:9001 ACTIVE
1 udma11 UB eid0 0000:0000:007f:0300:0010:0000:df00:9001 ACTIVE
2 udma2 UB eid0 0000:0000:003f:0200:0010:0000:df00:1001 ACTIVE
3 udma3 UB eid1 0000:0000:003f:0600:0010:0000:df00:1001 ACTIVE
4 udma3 UB eid2 0000:0000:0007:0600:0010:0000:df00:fd01 ACTIVE
5 udma3 UB eid3 0000:0000:0006:0600:0010:0000:df00:dd01 ACTIVE
6 udma3 UB eid4 0000:0000:0005:0600:0010:0000:df00:bd01 ACTIVE
7 udma3 UB eid5 0000:0000:0004:0600:0010:0000:df00:9d01 ACTIVE
8 udma3 UB eid6 0000:0000:0003:0600:0010:0000:df00:7d01 ACTIVE
9 udma3 UB eid7 0000:0000:0002:0600:0010:0000:df00:5d01 ACTIVE
10 udma3 UB eid8 0000:0000:0001:0600:0010:0000:df00:3d01 ACTIVE
11 udma4 UB eid0 0000:0000:003f:0500:0010:0000:df00:1001 ACTIVE
12 udma5 UB eid0 0000:0000:003f:0400:0010:0000:df00:1001 ACTIVE
13 udma6 UB eid0 0000:0000:003f:0300:0010:0000:df00:1001 ACTIVE
14 udma7 UB eid0 0000:0000:007f:0200:0010:0000:df00:9001 ACTIVE
15 udma8 UB eid0 0000:0000:0040:0600:0010:0000:df00:1e01 ACTIVE
16 udma8 UB eid1 0000:0000:007f:0600:0010:0000:df00:9001 ACTIVE
17 udma8 UB eid2 0000:0000:0047:0600:0010:0000:df00:fe01 ACTIVE
18 udma9 UB eid0 0000:0000:007f:0500:0010:0000:df00:9001 ACTIVE
```

#### Host PG EID 识别规则

**规则**：根据 `urma_admin show` 的输出，同一 CPU 上的所有 EID 具有相同的 die 标识。route.conf 中 `local_eid` 表示 NPU 所连接的 CPU 的 EID，通过解析该 EID 确定 CPU 所属 die，再选择对应的 Host PG EID。

**CPU 与 die 的对应关系**（由 `urma_admin show` 输出分析）：

| CPU | die | 对应 UDMA 设备 | PG EID 前缀 |
|-----|-----|---------------|-------------|
| CPU 0 | die 0 | udma8 eid1 | 007f:0600 |
| CPU 1 | die 1 | udma3 eid1 | 003f:0600 |

**映射分析示例**：

从 `urma_admin show` 输出中选取具有相同 die 标识的 EID：

- **udma8 eid1**: `0000:0000:007f:0600:0010:0000:df00:9001` → die 0 的 CPU（0-3 卡）
- **udma3 eid1**: `0000:0000:003f:0600:0010:0000:df00:9001` → die 1 的 CPU（4-7 卡）

**识别逻辑**：

- `local_eid`（route.conf）→ 确定 NPU 连接的 CPU 所属 die
- 根据 die 选择对应 CPU 的 PG EID

#### Host PG EID 与 route.conf local_eid 的映射关系

**route.conf 中的 local_eid（CPU EID）格式**：

`local_eid` 表示 NPU 所连接的 CPU 的 EID。通过该 EID 确定 CPU 所属 die，再选择对应的 Host PG EID。

**映射关系**：

```
通过 local_eid 确定 die_id 后，选择对应的 Host PG EID：

当 die_id == 0（0-3 卡）时：
  → Host PG EID 使用 udma8 eid1（007f:0600...）

当 die_id == 1（4-7 卡）时：
  → Host PG EID 使用 udma3 eid1（003f:0600...）
```

**选择逻辑**：

```
SelectHostPgEidByRouteData(entries, local_eid):
  1. 从 local_eid 分析确定 die_id（例如根据 EID 前缀中某字段）
  2. 在 urma_admin show 输出中找到 eid_count >= 8 的 UDMA 组（Host 侧）
  3. 根据 die_id 选择对应的 PG EID：
     - die_id == 0 → 选择 007f:0600... 前缀的 PG EID（udma8 eid1）
     - die_id == 1 → 选择 003f:0600... 前缀的 PG EID（udma3 eid1）
  4. 返回选中的 PG EID
```

#### 实现方案

**新增函数**：

```cpp
// 执行 urma_admin show 命令并解析输出
int32_t GetHostUrumaInfo(std::vector<UrumaDevEntry> &entries);

// 解析单行 urma_admin show 输出
bool ParseUrumaAdminLine(const std::string &line, UrumaDevEntry &entry);

// 根据 route_data 中的 local_eid 确定 Host die_id，选择对应的 PG EID
std::string SelectHostPgEidByRouteData(const std::vector<UrumaDevEntry> &entries,
                                       const std::string &local_eid);
```

**数据结构**：

```cpp
struct UrumaDevEntry {
    std::string udma_name;      // "udma3"
    int eid_index;              // eid0, eid1, ...
    std::string eid;            // 0000:0000:003f:0600:0010:0000:df00:1001
    int eid_count;              // 该 udma 设备的 eid 总数（判断是否为 Host 侧）
};
```

**选择逻辑**：

```
SelectHostPgEidByRouteData(entries, remote_eid):
  1. 从 remote_eid 末尾提取 die_id（"01" → 0, "81" → 1）
  2. 在 entries 中找到 eid_count >= 8 的 UDMA 组（Host 侧）
  3. 根据 die_id 选择对应的 PG EID：
     - die_id == 0 → 选择 003f:0200... 前缀的 PG EID
     - die_id == 1 → 选择 003f:0600... 前缀的 PG EID
  4. 返回选中的 PG EID
```

**修改 `GenerateH2UEdges`**：

```
原逻辑：
  H2U 边的 comm_id = Device 的 PG EID

新逻辑：
  H2U 边的 comm_id = Host 的 PG EID（通过 SelectHostPgEidByRouteData 获取）
```

#### 注意事项

- `urma_admin show` 命令需要 root 权限执行
- 需要处理命令执行失败的情况
- Host 可能有两个 PG EID（die 0 和 die 1），需要根据 route_data 中的 remote_eid 确定使用哪一个

---

## 三、接口依赖

### 现有 DCMI 接口

| 接口 | 用途 |
|------|------|
| `DcmiProxy::GetLogicIdFromPhyId` | NPU 物理 ID → 逻辑 ID |
| `DcmiProxy::GetMainboardId` | 获取主板 ID，判断产品形态 |
| `DcmiProxy::GetDeviceInfo` | 获取 SPOD 信息（super_pod_id） |
| `DcmiProxy::GetUrmaDeviceCnt` | 获取 URMA 设备数量 |
| `DcmiProxy::GetEidList` | 获取 EID 列表 |

### 需要新增的接口

| 接口 | 来源 | 用途 |
|------|------|------|
| `urma_admin show` | Host 命令行（通过 popen 执行） | 获取 Host 侧 URMA 组信息 |

---

## 四、文件修改清单

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `src/hixl/engine/local_comm_res_generator_v1.cc` | 修改 | Topo 路径选择、procfs fallback、H2U comm_id 纠错 |
| `src/hixl/engine/local_comm_res_generator_v1.h` | 修改 | 新增函数声明 |
| `src/hixl/engine/endpoint_generator.cc` | 修改 | 调用路径调整（如有） |

---

## 五、测试计划

| 测试场景 | 验证点 |
|---------|--------|
| Topo 文件路径 | mainboard_id=0x3/0x5/0x7/0x21 分别匹配正确的 topo 文件 |
| procfs fallback | 删除 `/lib/route.conf` 后能正常生成 RouteData |
| H2U 边 comm_id | 使用 Host PG EID 而非 Device PG EID |
| H2rD/H2rH 场景 | 建链成功验证（需端到端测试） |

---

## 六、风险与待确认项

1. **procfs 权限**：写入 `/proc/ascend_ub/dev_id` 需要 root 权限，需确认 fallback 是否作为可选项
2. **urma_admin show 权限**：该命令需要 root 权限执行
3. **Host PG EID 与 route.conf remote_eid 的映射关系**：通过 remote_eid 末尾 4 位区分 die_id，需验证
4. **产品形态与 topo 文件名映射表**：需与驱动团队确认各 mainboard_id 对应的文件名格式
