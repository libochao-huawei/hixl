# UBG 端到端联调用例与 hcomm 接口对齐

> **前置条件**：A5（Ascend950）硬件 + CANN ≥ 9.0.0，已 `source set_env.sh`，已 `bash build.sh --examples`
>
> **内存方向说明**：`D`=device，`H`=host，`r`=remote（对端）。write 格式 `{本端}2r{对端}`，read 格式 `r{对端}2{本端}`

---

## 一、端到端联调用例

### 1.1 功能用例 — 协议选取与冲突校验

| # | 场景 | 部署 | 命令 | 预期结果 | 优先级 |
|---|------|------|------|----------|--------|
| F1 | 跨超节点优先 UBOE>UBG>ROCE | 双机，net_instance_id 不同，两端均配 uboe+ubg+roce endpoint | `run_comm_benchmark.py --role=target`（默认 all transport）+ initiator 连接 | 日志显示匹配 UBOE 协议建链 | **P0** |
| F2 | 跨超节点无 UBOE 时选 UBG | 双机，仅配 ubg+roce | `--transport=ubg` 双机模式 | 日志显示匹配 UBG 协议建链 | **P0** |
| F3 | 同超节点优先 HCCS>UBG | 单机/同超节点双卡 | 默认配置（不指定 transport），两端 net_instance_id 相同 | 日志显示优先 HCCS（若有），否则 UBG | P1 |
| F4 | ProtocolLock 锁定 UBG | 双机，显式 protocol_desc=ubg:device | `--transport=ubg` 双机 | 即使有 UBOE 也仅匹配 UBG | P1 |
| F5 | HCCL_INTRA_ROCE_ENABLE=1 强制 RoCE | 双机，设环境变量 | `HCCL_INTRA_ROCE_ENABLE=1 python3 ... --transport=ubg` | 忽略 UBG，仅走 RoCE | P1 |
| F6 | ubg+uboe protocol_desc 冲突 | 单机双卡 | `python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --transport=ubg --type=D2rD --device_ids=0,1 -H GlobalResourceConfig='{"comm_resource_config.protocol_desc":["uboe:device","ubg:device"]}'` | 报 PARAM_INVALID（ubg+uboe 冲突） | P1 |
| F7 | UBG 断链重连 | 单机双卡 | 跑完一次传输后 Disconnect → 再次 Connect → TransferSync | 重连成功，数据正确 | P2 |

### 1.2 功能用例 — DSMI 自动生成（真实硬件）

| # | 场景 | 部署 | 命令 | 预期结果 | 优先级 |
|---|------|------|------|----------|--------|
| F10 | InterconType=4 自动生成 UBG | 单机/双机 A5，DSMI 返回 4 | 不传 protocol_desc，直接 Initialize，观察日志 | 日志打印 `InterconType=4`，自动生成 UBG endpoint | **P0** |
| F11 | InterconType=2 自动生成 UBoE | 单机 A5，DSMI 返回 2 | 同上 | 日志打印 `InterconType=2`，自动生成 UBoE endpoint | **P0** |
| F12 | InterconType=1 回退 RoCE | 单机 A5，DSMI 返回 1 | 同上 | 日志打印 `InterconType=1 is RoCE`，走旧 UB 生成路径 | P1 |
| F13 | InterconType 接口不可用 | 单机 A5，旧驱动 | 同上 | 日志打印 `DSMI InterconType not supported yet, fallback`，不报错 | P1 |

### 1.3 性能用例 — 单机

> 一次跑同时输出带宽（GB/s）和延迟（μs），block size 自动扫描 16K→2M 共 8 档。

| # | 场景 | 内存方向 | 执行命令 | 优先级 |
|---|------|----------|----------|--------|
| P1 | UBG D2D write | D2rD | `python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --transport=ubg --type=D2rD --device_ids=0,1 --soc_variant=a5 --loops=10` | **P0** |
| P2 | UBG D2D read | rD2D | 同上，`--type=rD2D` | **P0** |
| P3 | UBG H2H write | H2rH | 同上，`--type=H2rH` | **P0** |
| P4 | UBG H2H read | rH2H | 同上，`--type=rH2H` | **P0** |
| P5 | UBG H2D write | H2rD | 同上，`--type=H2rD` | P1 |
| P6 | UBG H2D read | rD2H | 同上，`--type=rD2H` | P1 |
| P7 | UBG D2H write | D2rH | 同上，`--type=D2rH` | P1 |
| P8 | UBG D2H read | rH2D | 同上，`--type=rH2D` | P1 |
| P9 | UBG 多卡 pairwise 聚合带宽 | D2rD | `--transport=ubg --type=D2rD --device_ids=0,1,2,3 --pattern=pairwise --loops=10` | P2 |
| P10 | UBG 多卡 one_to_many 聚合带宽 | D2rD | 同上，`--device_ids=0,1,2,3,4,5,6,7 --pattern=one_to_many` | P2 |
| P11 | UBG 多卡 many_to_one 聚合带宽 | D2rD | 同上，`--pattern=many_to_one` | P2 |

### 1.4 性能用例 — 双机

> 双机模式下 target 和 initiator 分处不同物理机。跨超节点场景 net_instance_id 不同。

| # | 场景 | 内存方向 | 执行命令 | 优先级 |
|---|------|----------|----------|--------|
| P20 | UBG 跨机 D2D write | D2rD | **target**: `--role=target --transport=ubg --type=D2rD --soc_variant=a5 --loops=10`<br>**initiator**: `--role=initiator --target-host=<ip> --transport=ubg --type=D2rD --soc_variant=a5 --loops=10` | **P0** |
| P21 | UBG 跨机 D2D read | rD2D | 同上，`--type=rD2D` | **P0** |
| P22 | UBG 跨机 H2H write | H2rH | 同上，`--type=H2rH` | **P0** |
| P23 | UBG 跨机 H2H read | rH2H | 同上，`--type=rH2H` | **P0** |
| P24 | UBG 跨机 H2D write | H2rD | 同上，`--type=H2rD` | P1 |
| P25 | UBG 跨机 H2D read | rD2H | 同上，`--type=rD2H` | P1 |
| P26 | UBG 跨机 D2H write | D2rH | 同上，`--type=D2rH` | P1 |
| P27 | UBG 跨机 D2H read | rH2D | 同上，`--type=rH2D` | P1 |
| P28 | UBG 跨机多卡 1:N 聚合带宽 | D2rD | **target**: `--role=target --pattern=one_to_many --device_ids=0,1,2,3`<br>**initiator**: `--role=initiator --pattern=one_to_many --num_initiators=4 --target-host=<ip>` | P2 |
| P29 | UBG 跨机多卡 N:1 聚合带宽 | D2rD | **target**: `--role=target --pattern=many_to_one`<br>4 个 initiator: `--role=initiator --target-host=<ip>` | P2 |

### 1.5 性能用例 — KV Cache

> KV benchmark 固定方向：put = device→remote host（D2rH），get = remote host→device（rH2D）。

| # | 场景 | 部署 | 执行命令 | 优先级 |
|---|------|------|----------|--------|
| P40 | UBG KV cache 基线 | 单机 8 卡 | `python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py --transport=ubg --platform=a5 --num_processes=8 --model=deepseek-r1 --key_counts=16,32,48,64 --repeat=10` | P1 |
| P41 | UBG KV cache 多模型 | 单机 8 卡 | 分别跑 `--model=deepseek-r1/glm5/deepseek-v4`，`--transport=ubg` | P2 |
| P42 | UBG KV cache 并发线程 | 单机 8 卡 | `--transport=ubg --transfer_threads=4/8/16` | P2 |

### 1.6 覆盖度总结

| 维度 | 单机 | 双机 | 覆盖状态 |
|------|------|------|----------|
| D2D write/read | P1-P2 | P20-P21 | 完整 |
| H2H write/read | P3-P4 | P22-P23 | 完整 |
| H2D/D2H write/read | P5-P8 | P24-P27 | 完整 |
| 多卡并发聚合 | P9-P11 | P28-P29 | 完整 |
| KV cache | P40-P42 | — | 完整 |
| 协议选取/冲突校验 | F1-F7 | F1-F5 | 完整 |
| DSMI 自动生成 | F10-F13 | F10 | 完整 |

---

## 二、hcomm 组件接口对齐（仅 UBG 差异项）

> 以下仅列出 UBG 与其他协议（UBoE/RoCE）存在差异、需要 hcomm 侧专项确认的接口。通用接口（ChannelDestroy、MemExport/Import、ThreadAlloc/Free、Write/Read/BatchTransfer/Fence、BatchMode 等）与协议无关，UBoE 已验证通过，UBG 无需重复验证。

### H1. HcommEndpointCreate — UBG 端点创建

**函数签名**：`HcclResult HcommEndpointCreate(const EndpointDesc *endpoint, EndpointHandle *endpoint_handle)`

**输入 — `EndpointDesc` 各字段**：

| 字段 | 类型 | UBG 取值 | 说明 |
|------|------|---------|------|
| `protocol` | `CommProtocol`(enum) | `COMM_PROTOCOL_UBG` (=9) | 新增枚举值，hcomm 需识别 |
| `commAddr.type` | `CommAddrType`(enum) | `COMM_ADDR_TYPE_EID` (=3) | EID 寻址（区别于 RoCE 的 IP_V4） |
| `commAddr.eid[0..15]` | `uint8_t[16]` | 16 字节原始 EID，`eid[7] & 0xC0 == 0x80` | 从 32 字符 hex 字符串转换而来（如 `0000000000ff0a80...`） |
| `loc.locType` | `EndpointLocType`(enum) | `ENDPOINT_LOC_TYPE_DEVICE` (=0) | UBG 当前仅支持 device 端点 |
| `loc.device.devPhyId` | `uint32_t` | 物理设备 ID（如 0,1,2...） | 从 `aclrtGetPhyDevIdByLogicDevId` 获取 |
| `loc.device.superDevId` | `uint32_t` | 超节点内 device ID | 从 `FillDeviceLocInfo` 填充 |
| `loc.device.serverIdx` | `uint32_t` | 0 | 当前固定为 0 |
| `loc.device.superPodIdx` | `uint32_t` | 超节点位置索引 | 从 `FillDeviceLocInfo` 填充 |
| `raws[52]` | `uint8_t[52]` | 未使用 | 协议扩展预留 |

**输出**：`EndpointHandle *endpoint_handle`（`void*`，不透明句柄）

**返回值**：`HCCL_SUCCESS`(0) = 成功

**hcomm 需确认**：是否已支持 `COMM_PROTOCOL_UBG`(=9) 枚举？按 EID 创建端点的逻辑是否与 UBoE 共用？

---

### H2. HcommChannelCreate — UBG 通道建立

**函数签名**：`HcclResult HcommChannelCreate(EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc *channel_descs, uint32_t channel_num, ChannelHandle *channels)`

**输入**：

| 参数 | 类型 | UBG 取值 | 说明 |
|------|------|---------|------|
| `endpoint_handle` | `EndpointHandle`(void*) | H1 返回的句柄 | 本端 UBG 端点 |
| `engine` | `CommEngine`(enum) | `COMM_ENGINE_AICPU`(=2) | device 端点走 AICPU 引擎 |
| `channel_num` | `uint32_t` | 1 | 单通道 |

**`HcommChannelDesc` 各字段**：

| 字段 | 类型 | UBG 取值 | 说明 |
|------|------|---------|------|
| `header.version` | `uint32_t` | 2 | ABI 版本号 |
| `header.magicWord` | `uint32_t` | `0x0fcf0f0f` | 魔数 |
| `remoteEndpoint` | `EndpointDesc` | 对端的完整 EndpointDesc | 含 `protocol=COMM_PROTOCOL_UBG`、`commAddr.eid[16]`（对端 EID） |
| `notifyNum` | `uint32_t` | 通知消息数 | channel 上的 notify 数量 |
| `exchangeAllMems` | `bool` | true/false | true 时自动交换全部已注册内存 |
| `role` | `HcommSocketRole`(enum) | `HCOMM_SOCKET_ROLE_SERVER`(=1) 或 `HCOMM_SOCKET_ROLE_CLIENT`(=0) | server 监听，client 发起连接 |
| `port` | `uint16_t` | 监听/目标端口 | server 侧为监听端口，client 侧为目标端口 |
| `qos` | `uint32_t` | QoS 值 | 通用 QoS（v2 新增） |

**输出**：`ChannelHandle *channels`（`uint64_t`，通道句柄）

**返回值**：`HCCL_SUCCESS`(0) = 成功

**hcomm 需确认**：两端 EID（16 字节 `memcmp` 匹配）建链是否正常？UBG 的 EID 建链路径与 UBoE 是否共用？

---

### H3. HcommMemReg — UBG 内存注册（含 host 映射差异）

**函数签名**：`HcclResult HcommMemReg(EndpointHandle endpoint_handle, const char *mem_tag, const CommMem *mem, HcommMemHandle *mem_handle)`

**输入**：

| 参数 | 类型 | UBG 取值 | 说明 |
|------|------|---------|------|
| `endpoint_handle` | `EndpointHandle` | H1 返回的句柄 | — |
| `mem_tag` | `const char*` | 内存标签字符串 | 用户传入的 tag |
| `mem` | `const CommMem*` | 见下方 | **UBG 场景下 type 和 addr 会被改写** |

**`CommMem` 各字段（UBG device 内存）**：

| 字段 | 类型 | 值 | 说明 |
|------|------|----|------|
| `type` | `CommMemType`(enum) | `COMM_MEM_TYPE_DEVICE` (=0) | 直接注册 |
| `addr` | `void*` | device 地址 | 用户原始地址 |
| `size` | `uint64_t` | 字节数 | 不变 |

**`CommMem` 各字段（UBG host 内存 — 关键差异）**：

| 字段 | 类型 | 用户传入 | **HIXL 改写后传入 hcomm** | 说明 |
|------|------|---------|------------------------|------|
| `type` | `CommMemType` | `COMM_MEM_TYPE_HOST` (=1) | **`COMM_MEM_TYPE_DEVICE` (=0)** | HIXL 先 `aclrtHostRegister` 映射，改为 device 类型 |
| `addr` | `void*` | host 地址 | **映射后的 device 地址** | `HostRegisterProxy::RegisterByDev` 返回值 |
| `size` | `uint64_t` | 字节数 | 不变 | — |

**输出**：`HcommMemHandle *mem_handle`（`void*`，内存句柄）

**返回值**：`HCCL_SUCCESS`(0) = 成功

**hcomm 需确认**：UBG + host 内存时 hcomm 收到的是映射后 device 地址（`type=DEVICE`），与 UBoE host 注册路径是否一致？

---

### 排查指引

| 故障现象 | 排查步骤 |
|----------|---------|
| EndpointCreate 返回错误 | 1. 打印 `endpoint->protocol` 确认为 9（`COMM_PROTOCOL_UBG`）；2. 打印 `endpoint->commAddr.eid[7] & 0xC0` 确认为 `0x80`；3. 确认 hcomm 库版本支持 UBG 枚举 |
| ChannelCreate 返回错误 | 1. 打印本端和对端 `commAddr.eid[16]` 确认一致；2. 确认 `remoteEndpoint.protocol` 为 9；3. 确认 `engine` 和 `port` 正确 |
| MemReg 返回错误（host 内存） | 1. 确认 HIXL 已调 `HostRegisterProxy::RegisterByDev` 成功；2. 打印传入 hcomm 的 `mem->type` 应为 `COMM_MEM_TYPE_DEVICE`(=0) 而非 HOST；3. 打印 `mem->addr` 应为映射后地址（非原始 host 地址） |

### 2.6 Transport 与 Initialize Option 对照

| transport | 环境变量 | Initialize Option（除默认 `BufferPool=0:0`） | 平台限制 |
|-----------|---------|------------------------------------------------------|---------|
| hccs | 无 | 无额外选项 | A2/A3（A5 禁用） |
| rdma | `HCCL_INTRA_ROCE_ENABLE=1` | 无额外选项 | 全平台 |
| fabric_mem | 无 | `EnableUseFabricMem=1` | 全平台 |
| uboe | 无 | `GlobalResourceConfig={"comm_resource_config.protocol_desc":["uboe:device"]}` | 仅 A5 |
| **ubg** | 无 | `GlobalResourceConfig={"comm_resource_config.protocol_desc":["ubg:device"]}` | 仅 A5 |
| ub | 无 | `LocalCommRes={"version":"1.3"}` | 仅 A5 |
