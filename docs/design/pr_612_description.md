# [feat]: UBG scale-out 协议支持

## 功能概述

新增 UBG（Unified Bus Gateway）ScaleOut 传输协议，用于 A5 平台跨超节点 NPU 间数据传输。UBG 走 NPU 内置 URMA 硬件口，以 EID 寻址，对标 UBoE 性能。

## 改动点

### 1. UBG Endpoint 自动生成
- 新增 `GenDefaultUbgEndpointConfig`：通过 DCMI 查询 URMA EID 列表，按 byte[7]&0xC0==0x80 过滤出 UBG EID
- 新增 `GetScaleOutNetInstanceId`：通过 DCMI 查询 super_pod_id，动态生成 net_instance_id（替代原 UBoE 硬编码 `"default_superpod1_1"`）
- 新增 `GenerateV5EndpointByInterconType`：无 protocol_desc 时按 DSMI InterconType 自动选择 UBG/UBoE/RoCE
- 新增 InterconType 交叉校验：protocol_desc=ubg 但 InterconType≠4 时报错

### 2. 协议匹配器扩展
- 跨实例和同实例匹配规则新增 UBG 条目
- `IsDirectProtocol` 新增 `kProtocolUbg`
- 优先级：跨超节点 UBoE>UBG>RoCE，同超节点 UB>HCCS>UBoE>UBG>RoCE

### 3. UBG/UBoE 互斥校验
- protocol_desc 层：ubg+uboe 同时配 → kConflict → 拒绝
- local_comm_res 层：endpoint_list 同时含 ubg+uboe → 拒绝
- ScaleOut 协议仅支持 device placement

### 4. HCCL_INTRA_ROCE_ENABLE 强制 RoCE 过滤
- 显式 `local_comm_res` 路径：过滤掉所有非 RoCE 端点
- V5 自动生成路径：跳过 ScaleOut 和 UB 生成

### 5. DSMI Proxy 扩展
- 新增 `GetInterconType()`：查 DSMI super_pod_intercon_type
- 新增 `IsInterconTypeSupported()`：判断驱动是否支持该接口（老驱动降级）
- `dsmi_get_device_info` 作为可选符号加载，失败不影响 `GetDevSlotId`

### 6. 其他
- `ConvertToEndpointDesc` UBG 分支走 `ParseEidAddress` + `FillDeviceLocInfo`
- `endpoint_store::operator==` 默认返回从 `true` 改为 `false`，修复未知协议误匹配
- 补充关键路径日志（eid/net_instance_id EVENT、EndpointCreate 完整信息）

## 测试

- 新增 `hixl_engine_ubg_unittest.cc`：H2H/H2D/D2H/D2D 端到端传输（4 用例）
- 扩展 endpoint_generator/matcher/engine/store/proxy UT
- 合计新增/修改 42 个测试用例

## 依赖

- hcomm 需包含 PR #2899（COMM_PROTOCOL_UBG=9 + UbgEndpoint + AicpuTsUbgChannel）
- 驱动需支持 DSMI `dsmi_get_device_info` 接口和 InterconType=4
