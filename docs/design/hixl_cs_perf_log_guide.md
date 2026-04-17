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

## 样例解读

假设日志中出现以下特征：

- `connect_total` 约 `80 ms`
- `local_create_channel` 与 `server_create_channel` 都在 `78 ms` 左右
- `tcp_connect`、`match_endpoint`、`get_remote_mem_total` 都低于 `2 ms`
- `transfer_submit_device` 很低，但 `transfer_sync_device` 和 `device_sync_wait` 很高
- 在总字节数固定时，`list_num` 从 `64` 增长到 `2048`，整体耗时显著上升

则可直接判断：

- 建链瓶颈在双端 `CreateChannel`，不在 TCP 或远端内存发现。
- Device 传输瓶颈主要在执行完成等待阶段，而不是提交阶段。
- 当前链路对分片数敏感，性能下降主要来自 per-op 开销累积，而不是纯链路带宽不足。

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
