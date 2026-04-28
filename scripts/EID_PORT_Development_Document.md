# EID & Port 计算逻辑开发文档

## 目标

实现一个 C++ 结构体，输入 `npu_id`，输出该 NPU 对应的 `eid` 和 `port` 映射信息。

```cpp
struct NpuEidPort {
    int npu_id;                                   // NPU 物理 ID
    std::vector<std::map<std::string, std::string>> eid_ports; // [{"eid": "...", "port": "die/port"}, ...]
};
```

## 核心概念

### EID 格式
EID 是一个 64 位十六进制数，通常表示为 16 个十六进制字符，分为若干字段：

```
0000:0000:0000:0000:0000:0000:0000:0000
```

关键字段位置（从右向左，0-indexed）：
- **最后 2 位 (index 14-15)**: 包含 port 信息
- **倒数第 2 位 (index 14)**: 包含 die_id 信息 (server 类型)
- **倒数第 3 位 (index 13)**: 部分 die_id 信息 (pod 类型)
- **特定位 (index 13 的特定位)**: fe_id、UBOE 标志

---

## 核心算法

### 1. 从 EID 提取 Port

**函数**: `get_eid_port(eid)` 或 `get_phy_port_by_eid(eid)`

```python
def get_eid_port(eid):
    """
    从 EID 的最后 2 位提取物理端口编号 (1-9)
    """
    last = eid[-2:]                    # 取最后 2 个十六进制字符
    h = int(last, 16)                  # 转为整数
    p = ((~128) & h) >> 3              # (~128) & h 清除最高位，然后右移 3 位
    return p
```

**等价 C++ 实现**:
```cpp
int get_eid_port(const std::string& eid) {
    // 取最后 2 位
    std::string last = eid.substr(eid.length() - 2);
    int h = std::stoi(last, nullptr, 16);
    int p = ((~128) & h) >> 3;
    return p;
}
```

**原理说明**:
- EID 最后 2 位形如 `0xAB`
- `~128` = `0x7F`（二进制 `01111111`）
- `(& h)` 清除 bit7（最高位）
- `>> 3` 右移 3 位，得到 port 编号 (0-9)

---

### 2. 从 EID 提取 Die ID (Server 类型)

**函数**: `get_server_die_id_by_eid(eid)` 或 `get_eid_die_id(eid)`

```python
def get_eid_die_id(eid):
    """
    从 EID 倒数第 2 位提取 die_id (server 类型)
    die_id = 0 或 1
    """
    low = eid[-2]                        # 取倒数第 2 个十六进制字符
    die_id = 8 & int(low, 16)            # 提取 bit3
    return die_id >> 3                  # 右移 3 位得到 0 或 1
```

**等价 C++ 实现**:
```cpp
int get_eid_die_id(const std::string& eid) {
    // 取倒数第 2 个字符
    char low = eid[eid.length() - 2];
    int h = std::stoi(std::string(1, low), nullptr, 16);
    int die_id = (8 & h) >> 3;
    return die_id;
}
```

---

### 3. 从 EID 提取 Die ID (Pod 类型)

**函数**: `get_pod_die_id_by_eid(eid)`

```python
def get_pod_die_id_by_eid(eid):
    """
    从 EID 倒数第 3 位提取 die_id (pod 类型)
    """
    h = int(eid[-3], 16)                # 取倒数第 3 个十六进制字符
    die_id = (4 & h) >> 2                # 提取 bit2
    return die_id
```

**等价 C++ 实现**:
```cpp
int get_pod_die_id_by_eid(const std::string& eid) {
    char third_from_last = eid[eid.length() - 3];
    int h = std::stoi(std::string(1, third_from_last), nullptr, 16);
    int die_id = (4 & h) >> 2;
    return die_id;
}
```

---

### 4. 构建 Port 字符串

根据 product_type 构建最终的 port 字符串，格式为 `"{die_id}/{port}"`：

**Server 类型** (port 需要 -1 映射到 0-8)：
```python
die_id = get_eid_die_id(eid)
p = get_eid_port(eid)
if p <= 9:
    port_str = f"{die_id}/{p - 1}"  # 例如 "1/3"
```

**Pod 类型**:
```python
die_id = get_eid_die_id(eid)
p = get_eid_port(eid)
if p <= 9 and (int(eid[-3], 16) & 8) == 0:
    port_str = f"{die_id}/{p - 1}"  # Pod v2 规则
```

---

## 完整处理流程

### CLOS 层（Level 1）Port 和 EID 获取方式

### 1. 获取 PG EID（CLOS 层 EID）

**函数**: `get_pg_eid(urma_device)`

```python
def get_pg_eid(self):
    """
    获取 CLOS 层的 EID（PG EID）
    CLOS 层的 EID 是串口组的标识，其 port > 9
    """
    for eid in self.eid_list:
        p = get_eid_port(eid)  # 复用 Mesh 层的 port 计算方法
        if p > 9:
            return eid
    raise ValueError(f"Failed to find PG EID for {self.name}")
```

**原理说明**:
- CLOS 层使用 port > 9 的 EID 作为串口组标识
- 这种 EID 代表的是一组串口的聚合，而不是单个物理串口
- 遍历 eid_list，找到第一个 port > 9 的 EID 即为 PG EID

---

### 2. CLOS 层 Port 配置

**CLOS 层是串口组**，多个物理串口组成一个组，用一个 PG EID 表示。

**Server 类型** - `get_level1_config()`:
```python
def get_level1_config():
    mainboard_id = ConfigMgr().get("mainboard_id")
    if mainboard_id == 35:  # 2+4 服务器
        return [(0, 3, (4,5,6,7)), (1, 2, (5,6))]
    if mainboard_id in (37, 39):
        return [(0, 3, (1,2,3,4,5,6,7,8))]
```

**Pod 类型** - `get_level1_config(phy_id)`:
```python
def get_level1_config(phy_id):
    mainboard_id = ConfigMgr().get("mainboard_id")
    if mainboard_id == 3 and (phy_id % 8) < 4:
        return [(0, 2, (1, 2)), (1, 2, (0,1,2,3,5,6))]
    if mainboard_id == 3 and (phy_id % 8) >= 4:
        return [(1, 2, (1, 2)), (0, 2, (0,1,2,3,4,5))]
```

**配置格式**: `(die_id, fe_id, (port_tuple))`
- `die_id`: 目标 die ID
- `fe_id`: 目标 FE ID
- `port_tuple`: 该 die/fe 组合对应的串口组

---

### 3. CLOS 层 Port 字符串构建

```python
def process_level1(npu):
    configs = get_level1_config()  # 或带 phy_id 参数
    for urma_dev in npu.urma_dev_list:
        fe_id = urma_dev.get_fe_id()
        try:
            die_id = urma_dev.get_die_id()
            eid = urma_dev.get_pg_eid()  # 获取 CLOS 层 EID
        except Exception:
            continue
        for target_die, target_fe, ports in configs:
            if die_id == target_die and fe_id == target_fe:
                # CLOS 层一个 EID 对应多个串口
                port_list = [f"{die_id}/{p}" for p in ports]
                plane_id = f"plane_pg_{0 if (len(ports) == 6) else 1}"
                npu.level(1).append(EID(eid, port_list, plane_id))
```

---

### 4. Mesh 层 vs CLOS 层对比

| 特性 | Mesh 层 (Level 0) | CLOS 层 (Level 1) |
|------|-------------------|-------------------|
| **EID 区分** | port ≤ 9 | port > 9 |
| **Port 类型** | 单个物理串口 | 串口组（多个串口聚合） |
| **Port 格式** | `["die/port"]` 单元素列表 | `["die/p1", "die/p2", ...]` 多元素列表 |
| **获取方法** | 从 eid_list 过滤 port ≤ 9 | `get_pg_eid()` 获取第一个 port > 9 的 EID |
| **plane_id** | `plane_{die_id}` | `plane_pg_{0或1}` |

---

## 完整处理流程

### 输入
- `npu_id`: NPU 物理 ID
- `eid_list`: 从 DCMI 接口获取的 EID 列表（按 URMA 分组）

### 处理逻辑

```
对于每个 npu_id:
    1. 调用 DCMI 接口获取 urma_dev_list (等价于 Python 的 get_urma_device_list)

    2. 对于每个 urma_dev (URMA 设备):
        a. 获取该设备的 eid_list

        b. 对于每个 eid:
            - 计算 port = get_eid_port(eid)
            - 计算 die_id = get_eid_die_id(eid)  [server]
                            或 get_pod_die_id_by_eid(eid)  [pod]

            - 如果 port 有效 (1-9):
                - port_str = f"{die_id}/{port - 1}"
                - 添加到结果列表: {"eid": eid, "port": port_str}

    3. 返回结构体: {npu_id: npu_id, eid_ports: [ {...}, ... ]}
```

### Port 有效性过滤条件

```python
# Server 类型
if p > 9:  # port 超过 9 则无效
    continue

# Pod 类型
if p > 9 or (int(eid[-3], 16) & 8) != 0:
    # port 超过 9 或 EID 中 bit3 置位则无效
    continue
```

---

## 数据结构对比

### Python (参考)
```python
class UrmaDevice:
    name: str
    product_type: str
    eid_list: List[str]

# 最终 EID 结构
class EID:
    eid: str
    ports: List[str]      # 格式 "die/port"
    plane_id: str
```

### C++ (目标)
```cpp
struct NpuEidPort {
    int npu_id;
    std::vector<std::map<std::string, std::string>> eid_ports;
    // 每个 map: {"eid": "0000:0000:0000:0000:0000:0000:0000:0000", "port": "1/3"}
};
```

---

## 关键文件映射

| Python 文件 | 功能 | 对应 C++ 模块 |
|-----------|------|--------------|
| `urma_device.py` | EID 基础计算函数 | DCMI 接口调用 |
| `product_server.py` | Server 类型的 port 计算 | Server 产品线 |
| `product_pod.py` | Pod 类型的 port 计算 | Pod 产品线 |
| `rootinfo.py` | 主流程编排 | 主流程 |

---

## 示例数据

### 输入 EID 列表
```
eid_list = [
    "0000:0000:0000:0000:0000:0000:0000:1000",
    "0000:0000:0000:0000:0000:0000:0000:0200",
    "0000:0000:0000:0000:0000:0000:0000:0030",
    "0000:0000:0000:0000:0000:0000:0000:0004"
]
```

### 计算过程
以 `eid = "0000:0000:0000:0000:0000:0000:0000:0200"` 为例：

1. **port 计算**:
   - 最后 2 位: `00`
   - `h = 0x00 = 0`
   - `p = (~128 & 0) >> 3 = 0`
   - port = 0

2. **die_id 计算**:
   - 倒数第 2 位: `0`
   - `h = 0`
   - `die_id = (8 & 0) >> 3 = 0`
   - die_id = 0

3. **port_str**: `"0/0"` (因为 p - 1 = 0 - 1 = -1，这里需要根据实际逻辑调整)

---

## 注意事项

1. **EID 格式**: 确保传入的 EID 是不带冒号或带冒号的统一格式
2. **Port 范围**: 有效 port 范围是 1-9，0 通常表示无效
3. **Server vs Pod**: 两种产品类型的 die_id 提取算法不同，需要根据 product_type 选择
4. **DCMI 接口**: 需要通过已有的 C++ DCMI 接口获取 urma_dev_list 和 eid_list
