# HIXL CS 性能统计日志解读

## 简介

本文档用于解读 `src/hixl/cs` 单边通信链路新增的性能统计日志。日志目标是覆盖 HIXL CS 的完整主链路：

- 初始化：`HixlCSServerCreate` / `HixlCSClientCreate`
- 建链：`HixlCSClientConnect`
- 远端内存发现与导入：`HixlCSClientGetRemoteMem`
- 传输提交：`HixlCSClientBatchPut/GetAsync/Sync`
- 完成态查询：`HixlCSClientQueryCompleteStatus`
- 销毁与清理：`HixlCSClientDestroy` / `HixlCSServerDestroy`

日志分为两类：

- 事件日志：单次调用、单个阶段的真实耗时
- 聚合摘要：周期性汇总同一 channel 的次数、最大耗时、平均耗时和总字节数

## 事件日志格式

事件日志统一为：

```text
HIXL CS PERF stage:<stage> role:<role> channel:<channel> fd:<fd> ch:<channel_handle> list_num:<n> bytes:<bytes> timeout_ms:<timeout> cost:<cost> us result:<ret>
```

常见字段含义：

- `stage`：阶段名
- `role`：`client_host`、`client_device` 或 `server`
- `channel`：统计聚合 key，客户端通常为 `client:<server_ip>:<server_port>`，服务端统一为 `server:<ip>:<port>`
- `fd`：控制面 socket
- `ch`：底层 HCCL channel handle
- `list_num`：本次批量传输的片段数
- `bytes`：本次批量传输总字节数
- `timeout_ms`：接口超时参数
- `cost`：耗时，单位微秒
- `result`：返回码

## 建链阶段解读

客户端建链阶段的关键打点：

- `client_create`
  表示客户端 `Create()` 初始化总耗时，不参与 `connect_total` 聚合。
- `tcp_connect`
  用于判断 TCP 建连本身是否慢。
- `match_endpoint`
  表示客户端发送 `MatchEndpointReq` 并收到 `MatchEndpointResp` 的总耗时。
- `get_remote_mem_rpc`
  表示远端内存描述符获取 RPC 的耗时。
- `import_remote_mem`
  表示客户端导入远端导出内存的耗时。
- `create_channel_req`
  表示发送 `CreateChannelReq` 的耗时。
- `local_create_channel`
  表示本端 `Endpoint::CreateChannel` 的耗时。
- `wait_create_channel_resp`
  表示等待服务端 `CreateChannelResp` 的耗时。
- `connect_total`
  表示整次 `Connect` 的总耗时。

如果 `connect_total` 高，但 `tcp_connect` 低，通常说明瓶颈在控制面处理或 channel 建立，而不是 TCP 建连。

如果 `client_create` 高而 `connect_total` 不高，优先排查客户端本地资源初始化，例如 device 资源准备、flag 队列或内存注册。

## 传输阶段解读

客户端传输阶段主要日志：

- `transfer_submit_host`
  Host 场景下，从提交 batch 到完成 flag 读取任务下发的耗时。
- `transfer_submit_device`
  Device 场景下，从获取 slot、准备参数、拉起 kernel 到 flag D2H 提交完成的耗时。
- `transfer_sync_host`
  Host 同步接口整体耗时。
- `transfer_sync_device`
  Device 同步接口整体耗时。
- `device_prepare_batch_mem`
  Device 路径准备批量 mem buffer 的耗时。
- `device_prepare_remote_flag`
  Device 路径获取远端完成 flag 和 kernel 元数据的耗时。
- `device_fill_args`
  Device 路径填充 kernel 参数的耗时。
- `device_launch_kernel`
  Device kernel 拉起和 notify 等待准备阶段的耗时。
- `device_sync_wait`
  Device 同步接口里 `aclrtSynchronizeStreamWithTimeout` 或异步完成等待阶段的耗时。
- `check_status_host`
  Host 异步完成态查询一次的耗时。
- `check_status_device`
  Device 异步完成态查询一次的耗时。

典型判断方法：

- `transfer_submit_*` 低但 `check_status_*` 长期为 WAITING
  多数是实际数据搬运或远端完成标志返回慢。
- `transfer_submit_device` 高
  优先结合 `device_prepare_batch_mem`、`device_prepare_remote_flag`、`device_fill_args`、`device_launch_kernel` 定位具体卡点。
- `transfer_sync_device` 高且 `device_sync_wait` 高
  多数是 stream 同步或异步完成等待阶段耗时高。
- 总字节数相同但 `list_num` 增大后 `device_prepare_batch_mem`、`device_fill_args` 或 `device_sync_wait` 明显升高
  说明性能对分片数敏感，瓶颈更偏向 per-op 开销，而不是纯带宽。

## 服务端阶段解读

服务端关键阶段：

- `server_initialize`
- `server_listen`
- `server_match_endpoint`
- `server_create_channel`
- `server_export_mem`
- `server_destroy_channel`
- `server_cleanup_client`
- `server_finalize`

排查时建议对齐客户端日志：

- 客户端 `match_endpoint` 对照服务端 `server_match_endpoint`
- 客户端 `get_remote_mem_rpc` 对照服务端 `server_export_mem`
- 客户端 `wait_create_channel_resp` 对照服务端 `server_create_channel`

如果客户端阶段慢而服务端对应阶段快，优先排查网络、控制面 socket、客户端反序列化或本端处理。

服务端聚合日志统一按 `server:<ip>:<port>` 输出，`fd` 只在事件日志中用于定位单个连接。

## 聚合摘要日志

聚合日志分三类：

```text
HIXL CS connect statistic info[...]
HIXL CS transfer statistic info[...]
HIXL CS server statistic info[...]
```

其中：

- `times`：累计次数
- `max cost`：最大耗时
- `avg cost`：平均耗时
- `client_create times / client_create avg cost`：客户端创建次数与平均耗时
- `total size`：累计传输字节数
- `avg size`：平均单次 op_desc 大小
- `bandwidth`：按 `total_bytes / total_cost` 估算的平均带宽
- `host times` / `device times`：Host / Device 传输次数
- `device_prepare_batch avg cost / device_prepare_flag avg cost / device_fill_args avg cost / device_launch avg cost / device_wait avg cost`
  Device 路径细分阶段的平均耗时

## 常见瓶颈模式

### 1. TCP 建连慢

现象：

- `tcp_connect` 高
- 服务端 `server_match_endpoint`、`server_create_channel` 都不高

优先排查：

- client 到 server 的网络连通性
- 监听地址配置
- socket 建连超时设置

### 2. 远端内存获取慢

现象：

- `get_remote_mem_rpc` 或 `server_export_mem` 高

优先排查：

- 服务端已注册内存数量是否过大
- JSON 序列化/反序列化开销
- 控制面消息体大小

### 3. 建 channel 慢

现象：

- `local_create_channel` 或 `server_create_channel` 高

优先排查：

- endpoint 配对是否合理
- 通道创建涉及的底层 HCCL/HCOMM 资源是否紧张

### 4. 传输完成慢

现象：

- `transfer_submit_*` 低
- `transfer_wait_complete` 或 `check_status_*` 高

优先排查：

- 实际链路带宽
- 远端完成 flag 返回路径
- Device 场景下 stream / notify / kernel 执行情况

## 性能分析样例：128 MB Device 单边通信

本样例基于一次 `128 MB` Device 单边通信测试。测试中 block size 依次为：

- `1 MB`
- `2 MB`
- `4 MB`
- `8 MB`
- `16 MB`
- `32 MB`

每个 block size 运行两轮，其中：

- 第一轮为预热
- 第二轮为正式性能结果

以下分析仅使用第二轮数据，且所有耗时分析均以 `HIXL CS PERF` 打点日志为准。

如果业务日志中的 `Transfer success ... time cost / throughput` 与 `HIXL CS PERF` 打点时间不一致，以 HIXL 打点口径为主。两者统计边界可能不同，外部打印可能包含或排除额外流程，不能与 `transfer_sync_device` 直接一一对应。

### 正式结果

| Block Size | List Num | transfer_sync_device | device_prepare_batch_mem | device_prepare_remote_flag | device_fill_args | device_launch_kernel | device_sync_wait | Perf Throughput | Transfer-Only Throughput |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MB | 128 | 3778 us | 153 us | 0 us | 108 us | 17 us | 3449 us | 33.086 GB/s | 36.242 GB/s |
| 2 MB | 64 | 3380 us | 148 us | 0 us | 108 us | 17 us | 3062 us | 36.982 GB/s | 40.823 GB/s |
| 4 MB | 32 | 3184 us | 146 us | 0 us | 112 us | 23 us | 2861 us | 39.259 GB/s | 43.691 GB/s |
| 8 MB | 16 | 3107 us | 146 us | 0 us | 112 us | 26 us | 2782 us | 40.232 GB/s | 44.932 GB/s |
| 16 MB | 8 | 3048 us | 136 us | 0 us | 112 us | 16 us | 2728 us | 41.010 GB/s | 45.821 GB/s |
| 32 MB | 4 | 3025 us | 153 us | 0 us | 107 us | 17 us | 2702 us | 41.322 GB/s | 46.262 GB/s |

其中：

- `Perf Throughput` 按 `128 MB / transfer_sync_device` 计算，表示端到端带宽
- `Transfer-Only Throughput` 按 `128 MB / device_sync_wait` 计算，表示仅按 device 实际同步等待阶段估算的带宽
- 本样例中，`device_sync_wait` 作为“具体传输用的时间”口径

在总数据量固定为 `128 MB` 时，第二轮端到端吞吐整体处于 `33 GB/s ~ 42 GB/s` 区间。随着 block size 增大、list num 减少，端到端吞吐持续提升；`16 MB ~ 32 MB` 为本次测试中的高性能区间。

### 建链阶段分析

本次建链相关日志如下：

- `client_create = 42.238 ms`
- `connect_total = 57.204 ms`
- `tcp_connect = 101 us`
- `match_endpoint = 223 us`
- `get_remote_mem_total = 938 us`
- `create_channel_req = 46 us`
- `local_create_channel = 55.586 ms`
- `wait_create_channel_resp = 242 us`
- `server_create_channel = 55.785 ms`

从上述数据可见，客户端 `connect_total` 为 `57.204 ms`，其中 `local_create_channel` 为 `55.586 ms`；服务端 `server_create_channel` 为 `55.785 ms`。与之相比，TCP 建连、Endpoint 匹配、远端内存导入导出都处于微秒到 `1 ms` 以内，不构成主要耗时。

建链阶段与传输阶段均采用同一套 `HIXL CS PERF` 口径，保证全文分析边界一致。

因此，本次建链耗时几乎全部集中在双端 `CreateChannel`：

- 建链总耗时约 `57.2 ms`
- Client `CreateChannel` 约 `55.6 ms`
- Server `CreateChannel` 约 `55.8 ms`
- 建链主瓶颈在 `CreateChannel`

### 传输阶段分析

第二轮各档位的 device 细分日志如下：

- `1 MB x 128`
  `device_prepare_batch_mem = 153 us`，`device_prepare_remote_flag = 0 us`，`device_fill_args = 108 us`，
  `device_launch_kernel = 17 us`，`device_sync_wait = 3449 us`，`transfer_sync_device = 3778 us`
- `2 MB x 64`
  `device_prepare_batch_mem = 148 us`，`device_prepare_remote_flag = 0 us`，`device_fill_args = 108 us`，
  `device_launch_kernel = 17 us`，`device_sync_wait = 3062 us`，`transfer_sync_device = 3380 us`
- `4 MB x 32`
  `device_prepare_batch_mem = 146 us`，`device_prepare_remote_flag = 0 us`，`device_fill_args = 112 us`，
  `device_launch_kernel = 23 us`，`device_sync_wait = 2861 us`，`transfer_sync_device = 3184 us`
- `8 MB x 16`
  `device_prepare_batch_mem = 146 us`，`device_prepare_remote_flag = 0 us`，`device_fill_args = 112 us`，
  `device_launch_kernel = 26 us`，`device_sync_wait = 2782 us`，`transfer_sync_device = 3107 us`
- `16 MB x 8`
  `device_prepare_batch_mem = 136 us`，`device_prepare_remote_flag = 0 us`，`device_fill_args = 112 us`，
  `device_launch_kernel = 16 us`，`device_sync_wait = 2728 us`，`transfer_sync_device = 3048 us`
- `32 MB x 4`
  `device_prepare_batch_mem = 153 us`，`device_prepare_remote_flag = 0 us`，`device_fill_args = 107 us`，
  `device_launch_kernel = 17 us`，`device_sync_wait = 2702 us`，`transfer_sync_device = 3025 us`

可以看到，`device_prepare_batch_mem`、`device_prepare_remote_flag`、`device_fill_args`、`device_launch_kernel` 的耗时整体都较小，通常在亚毫秒级；而 `device_sync_wait` 仍然明显占据主导。本次第二轮中 `device_fill_args` 稳定在 `107 us ~ 112 us`，未出现旧日志中的局部异常波动。

当总数据量固定为 `128 MB` 时，随着 `list_num` 从 `128` 下降到 `4`：

- `device_sync_wait` 从 `3.449 ms` 下降到 `2.702 ms`
- `transfer_sync_device` 从 `3.778 ms` 下降到 `3.025 ms`
- 按打点时间估算的端到端吞吐从 `33.086 GB/s` 提升到 `41.322 GB/s`

这说明当前链路性能仍然受分片数影响，但第二轮整体已经进入较高带宽区间，分片数下降带来的收益较第一轮更收敛。对于本样例，`transfer_sync_device` 是单次传输总耗时的统一分析口径，`device_sync_wait` 是其中的主要组成部分。

从带宽口径看：

- `Perf Throughput` 反映端到端带宽
- `Transfer-Only Throughput` 反映仅传输阶段带宽
- 两者差值反映的是提交准备阶段的额外开销，主要来自 `device_prepare_batch_mem` 和 `device_fill_args`

在 `128 MB` 这类中等数据量场景下，两种带宽差异较明显，说明除了看端到端带宽，也应看 `device_sync_wait` 口径带宽，以区分“准备开销”与“链路传输能力”。

### 聚合统计解读

本次聚合日志为：

- `avg total cost = 3528 us`
- `avg submit cost = 453 us`
- `avg wait cost = 3075 us`
- `device_prepare_batch avg cost = 246 us`
- `device_prepare_flag avg cost = 26 us`
- `device_fill_args avg cost = 113 us`
- `device_launch avg cost = 20 us`
- `device_wait avg cost = 3075 us`
- `bandwidth = 35.422 GB/s`

该聚合统计包含两轮共 `12` 次传输，因此适合用来判断整体趋势，不适合直接作为正式性能结果均值。

从聚合角度看，结论与单次日志一致：

- `submit` 阶段耗时很小
- `device_wait` 基本等于主要等待成本
- `device_prepare_batch` 和 `device_fill_args` 是主要提交准备开销，`device_launch` 较小
- 传输瓶颈主要消耗在 `device_wait`
- 聚合中的 `bandwidth` 可用于观察整体趋势，但本样例逐档结果以第二轮单次 `transfer_sync_device` 打点为准

若需要将聚合统计用于正式性能评估，建议在预热结束后清空统计，或将正式测试放在独立进程中执行。

### 结论

1. 本次 `hixl_cs` 128 MB Device 单边通信测试中，建链瓶颈在双端 `CreateChannel`。
2. 传输阶段的主耗时在 `device_sync_wait`，准备与启动阶段开销较小。
3. 第二轮正式结果中，端到端吞吐整体处于 `33 GB/s ~ 42 GB/s` 区间，`16 MB ~ 32 MB` 为更优配置区间。
4. 本次第二轮中 `device_fill_args` 稳定在 `107 us ~ 112 us`，未出现旧日志中的局部异常波动。
5. 若业务允许，应优先通过增大 block size、降低 `list_num` 来优化整体传输性能，但需要结合多轮结果排除单次波动。

## 性能分析样例：2 GB Device 单边通信

本样例基于一次 `2 GB` Device 单边通信测试。测试中 block size 依次为：

- `16 MB`
- `32 MB`
- `64 MB`
- `128 MB`
- `256 MB`
- `512 MB`

每个 block size 运行两轮，其中：

- 第一轮为预热
- 第二轮为正式性能结果

以下分析仅使用第二轮数据，且所有耗时分析均以 `HIXL CS PERF` 打点日志为准。

如果业务日志中的 `Transfer success ... time cost / throughput` 与 `HIXL CS PERF` 打点时间不一致，以 HIXL 打点口径为主。两者统计边界可能不同，外部打印可能包含或排除额外流程，不能与 `transfer_sync_device` 直接一一对应。

### 正式结果

| Block Size | List Num | transfer_sync_device | device_sync_wait | Perf Throughput | Transfer-Only Throughput |
|---|---:|---:|---:|---:|---:|
| 16 MB | 128 | 43753 us | 43381 us | 45.711 GB/s | 46.103 GB/s |
| 32 MB | 64 | 43445 us | 43001 us | 46.035 GB/s | 46.511 GB/s |
| 64 MB | 32 | 43186 us | 42807 us | 46.311 GB/s | 46.721 GB/s |
| 128 MB | 16 | 43090 us | 42716 us | 46.414 GB/s | 46.821 GB/s |
| 256 MB | 8 | 43030 us | 42665 us | 46.479 GB/s | 46.877 GB/s |
| 512 MB | 4 | 43040 us | 42649 us | 46.468 GB/s | 46.894 GB/s |

其中：

- `Perf Throughput` 按 `2 GB / transfer_sync_device` 计算，表示端到端带宽
- `Transfer-Only Throughput` 按 `2 GB / device_sync_wait` 计算，表示仅按 device 实际同步等待阶段估算的带宽
- 本样例中，`device_sync_wait` 作为“具体传输用的时间”口径

在总数据量固定为 `2 GB` 时，第二轮正式结果整体已进入稳定高带宽区间。`16 MB x 128` 已达到约 `45.7 GB/s`，继续增大 block size 到 `512 MB` 的收益很小，说明链路已接近本次环境中的带宽上限区间。

### 建链阶段分析

本次建链相关日志如下：

- `client_create = 41.241 ms`
- `connect_total = 55.540 ms`
- `tcp_connect = 98 us`
- `match_endpoint = 244 us`
- `get_remote_mem_total = 831 us`
- `create_channel_req = 49 us`
- `local_create_channel = 54.204 ms`
- `wait_create_channel_resp = 44 us`
- `server_create_channel = 53.891 ms`

从上述数据可见，客户端 `connect_total` 为 `55.540 ms`，其中 `local_create_channel` 为 `54.204 ms`；服务端 `server_create_channel` 为 `53.891 ms`。与之相比，TCP 建连、Endpoint 匹配、远端内存导入导出都处于微秒到 `1 ms` 以内，不构成主要耗时。

因此，`2 GB` 大包场景下建链瓶颈仍然没有变化：

- 建链总耗时约 `55.5 ms`
- Client `CreateChannel` 约 `54.2 ms`
- Server `CreateChannel` 约 `53.9 ms`
- 建链主瓶颈依旧在 `CreateChannel`

这说明本次 `2 GB` 场景与 `128 MB` 场景相比，主要变化发生在传输阶段，而不是建链阶段。

### 传输阶段分析

第二轮各档位的核心 device 同步日志如下：

- `16 MB x 128`
  `device_sync_wait = 43381 us`，`transfer_sync_device = 43753 us`
- `32 MB x 64`
  `device_sync_wait = 43001 us`，`transfer_sync_device = 43445 us`
- `64 MB x 32`
  `device_sync_wait = 42807 us`，`transfer_sync_device = 43186 us`
- `128 MB x 16`
  `device_sync_wait = 42716 us`，`transfer_sync_device = 43090 us`
- `256 MB x 8`
  `device_sync_wait = 42665 us`，`transfer_sync_device = 43030 us`
- `512 MB x 4`
  `device_sync_wait = 42649 us`，`transfer_sync_device = 43040 us`

可以看到，`device_sync_wait` 仍然是绝对主耗时。结合聚合统计中的 `device_prepare_batch`、`device_prepare_flag`、`device_fill_args` 和 `device_launch` 均值，准备与启动阶段开销占比很低。

本次正式结果只采用第二轮 `HIXL CS PERF` 打点。第一轮中的 `16 MB x 16`、`256 MB x 8` 等局部波动属于预热轮或非正式统计口径，不纳入正式结果。

但与 `128 MB` 样例不同，`2 GB` 场景下随着 `list_num` 从 `128` 降到 `4`：

- `device_sync_wait` 只从 `43.381 ms` 下降到 `42.649 ms`
- `transfer_sync_device` 只从 `43.753 ms` 下降到 `43.040 ms`
- 按打点时间估算的吞吐从 `45.711 GB/s` 提升到 `46.468 GB/s`

这说明在 `2 GB` 总数据量下，分片数减少仍然能带来收益，但收益已经明显小于 `128 MB` 样例。链路仍然受 `device_sync_wait` 主导，但此时已经进入稳定高带宽区间，per-op 开销占比已经下降，继续增大 block size 的优化空间有限。

从带宽口径看：

- `Transfer-Only Throughput` 略高于 `Perf Throughput`
- 两者差值整体较小
- 说明在大包场景下，准备开销占比更低，整体更接近真实链路带宽上限

### 聚合统计解读

本次聚合日志为：

- `avg total cost = 44111 us`
- `avg submit cost = 701 us`
- `avg wait cost = 43409 us`
- `device_prepare_batch avg cost = 245 us`
- `device_prepare_flag avg cost = 25 us`
- `device_fill_args avg cost = 334 us`
- `device_launch avg cost = 38 us`
- `device_wait avg cost = 43409 us`
- `bandwidth = 45.340 GB/s`

该聚合统计包含两轮共 `12` 次传输，因此适合用来判断整体趋势，不适合直接作为正式性能结果均值。

从聚合角度看，结论与单次日志一致：

- `avg submit cost` 远小于 `avg wait cost`
- `device_wait` 仍然是绝对主项
- 聚合带宽 `45.340 GB/s` 与第二轮逐档按打点时间估算的吞吐趋势一致
- 该场景已经进入稳定高带宽区间

本样例中，单档分析仍以第二轮单次 `transfer_sync_device` 打点为准。

### 结论

1. 本次 `2 GB` Device 单边通信测试中，建链瓶颈依旧在双端 `CreateChannel`。
2. 传输阶段的主耗时仍然在 `device_sync_wait`，准备与启动阶段开销依旧很小。
3. 第二轮正式结果整体已进入稳定高带宽区间，`16 MB` block 已达到约 `45.7 GB/s`。
4. 从 `16 MB x 128` 增大到 `512 MB x 4` 后，端到端吞吐只从 `45.711 GB/s` 提升到 `46.468 GB/s`，边际收益很小。
5. 对业务调优而言，中小包场景优先减少分片数；`2 GB` 大包场景更多体现真实链路带宽上限，继续放大单块大小的收益会逐渐收敛。
6. `2 GB` 大包场景里，端到端带宽与仅传输阶段带宽已经比较接近，进一步说明瓶颈主要集中在稳定的 `device_sync_wait` 阶段。

## Host 与 Device 差异

- Host 路径通过远端完成 flag 回读判断完成，重点看 `transfer_submit_host` 和 `check_status_host`
- Device 路径通过 `TransferPool + kernel + notify + host_flag` 完成判定，重点看 `transfer_submit_device`、`device_prepare_*`、`device_launch_kernel`、`device_sync_wait`、`transfer_sync_device` 和 `check_status_device`

## 建议使用方式

建议在同一时间窗口同时抓取 client 和 server 的 `HIXL CS PERF` 与 `HIXL CS .* statistic info` 日志，再按 `channel`、`fd` 和时间顺序进行比对，可较快定位卡点是在：

- TCP 建连
- 控制面匹配
- 内存导出/导入
- channel 创建
- 数据传输提交
- 完成态返回
