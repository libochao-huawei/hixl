# HIXL Benchmark 重构计划

## 背景

当前 `benchmarks` 目录已经包含基础 HIXL 传输 benchmark 和 FabricMem KV benchmark，但整体能力仍偏单点：

- 纯通信 benchmark 主要面向 client/server 单组传输，缺少统一的多拓扑抽象。
- FabricMem 作为独立 benchmark 存在，和普通通信 benchmark、KV 语义 benchmark 没有统一参数体系。
- KV 场景 benchmark 需要进一步贴近真实模型的 KV cache 组织方式，而不是简单用 `bytes_per_token` 描述。
- benchmark 结果需要能直接用于性能分析和回归对比，除了文本日志，还需要输出结构化结果和性能折线图。

本次重构目标是在 `benchmarks` 下提供三类 benchmark：

1. 纯通信语义 benchmark，默认 1:1，target 和 initiator 之间使用 p2p metadata，参考 Mooncake `tebench`。
2. many-to-one / one-to-many 通信语义 benchmark，使用 HTTP metadata server 和脚本统一启动。
3. KV 语义 benchmark，放在独立 `kv_benchmark` 目录，实现轻量级 `kvstore`，模拟不同模型和不同 token 长度下的 KV cache put/get 性能。

FabricMem 不再作为单独 benchmark 模式存在，而是融合到所有 benchmark 的传输参数中。

## 目录规划

建议重构后的目录结构如下：

```text
benchmarks/
├── comm_benchmark/
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── hixl_comm_bench.cpp
│   ├── common/
│   │   ├── benchmark_config.h
│   │   ├── benchmark_config.cpp
│   │   ├── benchmark_runner.h
│   │   ├── benchmark_stats.h
│   │   ├── benchmark_stats.cpp
│   │   ├── memory_manager.h
│   │   ├── memory_manager.cpp
│   │   ├── transfer_runner.h
│   │   └── transfer_runner.cpp
│   ├── metadata/
│   │   ├── p2p_metadata.h
│   │   ├── p2p_metadata.cpp
│   │   ├── http_metadata_client.h
│   │   ├── http_metadata_client.cpp
│   │   └── http_metadata_server.py
│   ├── scripts/
│   │   ├── run_comm_benchmark.py
│   │   └── plot_comm_benchmark.py
│   └── output/
│       └── .gitignore
│
├── kv_benchmark/
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── hixl_kv_bench.cpp
│   ├── kvstore/
│   │   ├── kvstore.h
│   │   ├── kvstore.cpp
│   │   ├── segment_manager.h
│   │   ├── segment_manager.cpp
│   │   ├── model_config.h
│   │   └── model_config.cpp
│   ├── config/
│   │   └── models.json
│   ├── scripts/
│   │   ├── run_kv_benchmark.py
│   │   └── plot_kv_benchmark.py
│   └── output/
│       └── .gitignore
│
└── benchmark.md
```

`comm_benchmark` 统一承载 1:1、many-to-one、one-to-many 三种纯通信语义，核心代码复用。`kv_benchmark` 单独放置，因为它测试的是 KV cache 业务语义，不应和纯通信 benchmark 的参数、统计口径混在一起。

## 一、纯通信语义 Benchmark

### 目标

提供最基础、最稳定、最容易排障的 HIXL 通信 benchmark。默认拓扑为：

- 1 个 target
- 1 个 initiator
- metadata 使用 p2p
- 不依赖 HTTP metadata server
- 默认测试 1:1 通信性能

该 benchmark 主要用于：

- 验证 HIXL 建链和基础传输是否正常。
- 快速比较不同 block size、batch size、并发线程下的带宽和延迟。
- 作为开发过程中的 smoke test 和性能回归入口。

### 运行方式

target：

```bash
./hixl_comm_bench \
  --role=target \
  --metadata=p2p \
  --local_engine=127.0.0.1:16000 \
  --memory_type=device \
  --transport=hccs
```

target 启动后打印 initiator 命令：

```text
To start initiator:
  ./hixl_comm_bench --role=initiator --metadata=p2p --target_id=<TARGET_ID> ...
```

initiator：

```bash
./hixl_comm_bench \
  --role=initiator \
  --metadata=p2p \
  --target_id=<TARGET_ID> \
  --local_engine=127.0.0.1:16001 \
  --op_type=write \
  --start_block_size=4096 \
  --max_block_size=67108864 \
  --start_batch_size=1 \
  --max_batch_size=128 \
  --start_threads=1 \
  --max_threads=8 \
  --duration=5
```

### 参数设计

基础参数：

- `--role=target|initiator`
- `--metadata=p2p|http`
- `--benchmark_group=<name>`
- `--local_engine=<endpoint>`
- `--target_id=<id>`
- `--op_type=read|write|mix`
- `--duration=<seconds>`
- `--warmup_duration=<seconds>`
- `--check_consistency=true|false`
- `--seed=<uint64>`

扫描参数：

- `--start_block_size=<bytes>`
- `--max_block_size=<bytes>`
- `--start_batch_size=<num>`
- `--max_batch_size=<num>`
- `--start_threads=<num>`
- `--max_threads=<num>`

传输参数：

- `--transport=hccs|rdma|fabric_mem`
- `--memory_type=host|device`

`fabric_mem` 是传输路径，不是内存类型。`--memory_type` 只描述本地 buffer 从 host 还是 device 分配；是否走 FabricMem 由 `--transport=fabric_mem` 控制，不再单独维护 `fabric_mem` 专用 benchmark。

### 输出指标

每个 `(block_size, batch_size, threads)` 输出一行结果：

- `BlockSize`
- `BatchSize`
- `Threads`
- `OpType`
- `Transport`
- `MemoryType`
- `BandwidthGBps`
- `OpsPerSec`
- `AvgLatencyUs`
- `AvgTransferUs`
- `P50TransferUs`
- `P90TransferUs`
- `P99TransferUs`
- `P999TransferUs`
- `ErrorCount`
- `Consistency`

同时输出结构化结果：

```text
output/
├── comm_result_<timestamp>.json
├── comm_result_<timestamp>.csv
└── comm_result_<timestamp>.png
```

性能折线图要求：

- 横轴默认为 `BlockSize`。
- 纵轴至少包含 `BandwidthGBps` 和 `P99TransferUs` 两张图。
- 多条曲线按 `batch_size`、`threads`、`transport` 或 `memory_type` 区分。
- 脚本支持从 CSV/JSON 重新生成图，避免 benchmark 跑完后只能看日志。

## 二、Many-to-One / One-to-Many 通信语义 Benchmark

### 目标

在纯通信 benchmark 的基础上增加多拓扑能力：

- `one_to_many`：1 个 initiator 对 N 个 target。
- `many_to_one`：N 个 initiator 对 1 个 target。

该模式使用 HTTP metadata server 进行：

- worker 注册
- segment 注册
- peer 发现
- benchmark group 隔离
- barrier 同步
- benchmark 完成后的 metadata 清理

### HTTP Metadata Server

提供轻量级 HTTP metadata server：

```bash
python3 comm_benchmark/metadata/http_metadata_server.py \
  --host=0.0.0.0 \
  --port=18080
```

建议 metadata key 结构：

```text
/groups/{group_id}/workers/{worker_id}
/groups/{group_id}/segments/{segment_id}
/groups/{group_id}/barriers/{barrier_name}
/groups/{group_id}/results/{worker_id}
```

server 只负责 benchmark 进程协同，不参与数据面传输。

### 脚本启动

用户不直接手工启动 N 个 benchmark 进程，而是通过脚本配置拓扑。

one-to-many：

```bash
python3 comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=one_to_many \
  --num_targets=4 \
  --metadata_server=http://127.0.0.1:18080 \
  --op_type=write \
  --block_size=1048576 \
  --batch_size=32 \
  --threads=4 \
  --duration=10 \
  --transport=fabric_mem
```

many-to-one：

```bash
python3 comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=many_to_one \
  --num_initiators=4 \
  --metadata_server=http://127.0.0.1:18080 \
  --op_type=read \
  --block_size=1048576 \
  --batch_size=32 \
  --threads=4 \
  --duration=10 \
  --memory_type=device
```

### 代码复用

1:1、one-to-many、many-to-one 必须共用以下模块：

- `BenchmarkConfig`
- `BenchmarkStats`
- `MemoryManager`
- `TransferRunner`
- `P2pMetadata` / `HttpMetadataClient` 的统一 metadata interface
- 结果输出和画图脚本

差异只体现在：

- metadata backend：p2p 或 HTTP
- peer selection：单 peer 或 peer 列表
- transfer pattern：pairwise、one-to-many、many-to-one

### 输出和折线图

多拓扑 benchmark 输出：

- 每个 worker 的局部结果。
- group 聚合结果。
- 总带宽。
- 每个 peer 的带宽分布。
- P50/P90/P99/P999 transfer latency。
- 错误数和 consistency 结果。

折线图要求：

- `one_to_many`：横轴可为 target 数或 block size，纵轴为总带宽、单 target 平均带宽、P99。
- `many_to_one`：横轴可为 initiator 数或 block size，纵轴为总带宽、单 initiator 平均带宽、P99。
- 输出 PNG，同时保留 CSV/JSON。

## 三、KV 语义 Benchmark

### 目标

`kv_benchmark` 用于模拟 KV cache 场景，不测试裸通信接口，而测试轻量级 KV store 语义：

- 一个 key 对应 128 tokens 的 KV cache。
- benchmark 按 token 长度测试，例如 16K、32K、64K、128K。
- 不同模型通过“层数 + 每层多个 cache 大小”描述，而不是简单 `kv_bytes_per_token`。
- `kvstore` 底层使用 HIXL 完成数据传输。
- `batch_put_from_multi_buffers` 和 `batch_get_into_multi_buffers` 是 `kvstore` 对外接口，语义参考 Mooncake Store 的接口原型，不是 HIXL 原生接口。
- `kvstore` benchmark 不区分 target / initiator。它由脚本统一启动多个对等进程，默认 8 个进程对应 8 个 device。
- 默认每个进程管理 1 个 segment，并注册 10 GiB host 内存作为 KV cache pool。
- KV store 的传输方向固定为 `d2rh` / `rh2d`：`put` 表示从 device 写入 remote host pool，`get` 表示从 remote host pool 读回 device。

### 模型抽象

模型 KV cache 不应抽象成单个 `kv_bytes_per_token`。不同模型可能有多层，每层有多个 cache，每个 cache 大小可能不同。统一抽象为：

```cpp
struct CacheSpec {
  std::string name;
  size_t bytes_per_key;
};

struct LayerSpec {
  std::vector<CacheSpec> caches;
};

struct ModelSpec {
  std::string name;
  std::vector<LayerSpec> layers;
  size_t tokens_per_key;
};
```

语义：

- `tokens_per_key` 固定为 128。
- 一个 key 的 value size 为所有 layer、所有 cache 的 `bytes_per_key` 之和。
- `CacheSpec::name` 只用于可读性和结果记录，不参与传输逻辑。
- 模型差异只影响每个 key 的 KV cache 总大小和 buffer layout。

以 DeepSeek-R1 为例：

- 层数：61。
- 每层包含 2 个 cache。
- 每 128 tokens：
  - rope cache：16 KiB。
  - latent cache：128 KiB。
- 单 key 大小：

```text
61 * (16 KiB + 128 KiB) = 8784 KiB
```

后续支持的模型：

- `deepseek-r1`
- `kimi-k2.5`
- `glm5`

`deepseek-r1`、`kimi-k2.5` 和 `glm5` 的层数、每层 cache 数量、每个 cache 的 128-token 大小统一写在 `kv_benchmark/config/models.json` 中。C++ 只负责解析配置和计算 key size，避免在 benchmark 逻辑里散落模型参数。

### Token 长度测试矩阵

KV benchmark 按 token 长度测试，而不是只按单 key 大小测试。

固定规则：

```text
1 key = 128 tokens
key_count = token_length / 128
```

默认 token 长度：

- `16K tokens`：128 keys。也可理解为 `4K tokens = 32 keys`，所以 `16K = 32 * 4 keys`。
- `32K tokens`：256 keys。
- `64K tokens`：512 keys。
- `128K tokens`：1024 keys。

建议参数：

```bash
--token_lengths=16K,32K,64K,128K
--tokens_per_key=128
```

每个 token length 下，benchmark 需要生成对应数量的 key，并对这些 key 做 batch put/get。

### kvstore 设计

`kvstore` 是 benchmark 内的轻量级 KV store，不实现完整 Mooncake Store，只实现 benchmark 所需能力。

职责：

- 管理多个 segment。
- 管理 key 到 segment 的映射。
- `batch_put_from_multi_buffers` 时为每个 key 随机选择 segment。
- 支持 `--seed`，保证相同 key 集合、相同 segment 数、相同 seed 下分配结果可复现。
- 记录 key 的 segment、offset、size。
- 通过 HIXL 执行底层传输。
- 默认由 8 个进程共同构成 KV store，每个进程绑定 1 个 device，持有 1 个 10 GiB host segment。

建议核心结构：

```cpp
struct KeyPlacement {
  std::string key;
  uint32_t segment_id;
  uint64_t offset;
  uint64_t size;
};
```

随机分配策略：

- 初始版本使用 seed 固定的伪随机数生成器。
- 每个 key 随机选择一个 segment。
- segment 内 offset 由 `SegmentManager` 分配。
- 如果某个 segment 空间不足，则按固定顺序尝试下一个 segment。
- 所有随机路径必须只由 `--seed` 决定，保证复现。

### kvstore 接口

`batch_put_from_multi_buffers` 和 `batch_get_into_multi_buffers` 属于 `kvstore`，不是 HIXL 接口。

接口语义参考 Mooncake Store：

```cpp
class KvStore {
 public:
  Status batch_put_from_multi_buffers(
      const std::vector<std::string>& keys,
      const std::vector<BufferView>& source_buffers);

  Status batch_get_into_multi_buffers(
      const std::vector<std::string>& keys,
      const std::vector<BufferView>& destination_buffers);
};
```

其中：

- `source_buffers[i]` 是第 `i` 个 key 的本地 KV cache buffer。
- `destination_buffers[i]` 是第 `i` 个 key 的本地接收 buffer。
- `kvstore` 根据 key placement 将每个 key 转换成底层 HIXL transfer request。
- HIXL 只负责传输，不理解 key、segment 分配和 KV 语义。

### 运行方式

KV benchmark 只通过脚本启动，不暴露 target / initiator 两类手工进程。默认启动 8 个进程，对应 8 个 device，每个进程注册 1 个 10 GiB host segment：

```bash
python3 kv_benchmark/scripts/run_kv_benchmark.py \
  --num_processes=8 \
  --devices=0,1,2,3,4,5,6,7 \
  --segment_size=10G \
  --pool_memory=host \
  --model=deepseek-r1 \
  --model_config=kv_benchmark/config/models.json \
  --token_lengths=16K,32K,64K,128K \
  --batch_size=128 \
  --op_type=put_get \
  --seed=1234 \
  --transport=hccs
```

传输语义：

- `put`：device buffer 到 remote host pool，传输类型为 `d2rh`。
- `get`：remote host pool 到 device buffer，传输类型为 `rh2d`。
- 每个进程既可以作为某些 key 的本地发起方，也可以作为其他 key 的 remote host pool 持有方，角色由脚本和 `kvstore` 的 placement 决定，不再用 target/initiator 命名。

### 输出指标

每个 `(model, token_length, batch_size, segment_count, transport)` 输出：

- `Model`
- `TokenLength`
- `KeyCount`
- `TokensPerKey`
- `KeySizeBytes`
- `TotalBytes`
- `BatchSize`
- `ProcessCount`
- `DeviceCount`
- `SegmentCount`
- `SegmentSize`
- `PoolMemory`
- `PutTransferType`
- `GetTransferType`
- `PutBandwidthGBps`
- `GetBandwidthGBps`
- `PutOpsPerSec`
- `GetOpsPerSec`
- `PutAvgLatencyUs`
- `GetAvgLatencyUs`
- `PutP50LatencyUs`
- `PutP99LatencyUs`
- `GetP50LatencyUs`
- `GetP99LatencyUs`
- `SegmentDistribution`
- `Consistency`

输出文件：

```text
kv_benchmark/output/
├── kv_result_<timestamp>.json
├── kv_result_<timestamp>.csv
├── kv_put_bandwidth_<timestamp>.png
├── kv_get_bandwidth_<timestamp>.png
├── kv_put_p99_<timestamp>.png
└── kv_get_p99_<timestamp>.png
```

折线图要求：

- 横轴默认为 `TokenLength`。
- `put` 和 `get` 分开画图。
- 每个模型一条曲线，或每个 transport / memory type 一条曲线。
- 至少输出四张图：
  - token length vs put bandwidth
  - token length vs get bandwidth
  - token length vs put P99 latency
  - token length vs get P99 latency

## 四、FabricMem 融合方案

FabricMem 不再作为独立 benchmark。

统一作为传输路径进入 benchmark：

```bash
--transport=fabric_mem
```

`--memory_type=fabric_mem` 不再使用，因为 FabricMem 不是一种普通 buffer 分配位置。`--memory_type` 保留为 `host|device`，只描述普通本地 buffer 类型；FabricMem 相关能力由 `--transport=fabric_mem` 或后续等价的 FabricMem 传输开关表达。

融合范围：

- 1:1 纯通信 benchmark。
- one-to-many 通信 benchmark。
- many-to-one 通信 benchmark。
- KV 语义 benchmark。

实现要求：

- FabricMem 初始化、物理内存申请、share handle 导出/导入、map/unmap 封装在公共 memory/transport 模块。
- benchmark 主逻辑不直接调用 ACL FabricMem 细节。
- 同一组 benchmark 参数可以只切换 `transport` 来对比普通 HCCS/RDMA 路径和 FabricMem 路径。
- 输出结果必须记录 `transport` 和 `memory_type`，折线图也支持按这两个字段分组。

## 五、结果文件和折线图

所有 benchmark 都必须输出：

- 控制台 summary。
- JSON，保留完整元数据。
- CSV，用于后处理。
- PNG 折线图。

推荐 JSON 顶层结构：

```json
{
  "benchmark_name": "hixl_kv_bench",
  "timestamp": "2026-05-07T16:00:00+08:00",
  "config": {},
  "environment": {},
  "results": []
}
```

推荐 CSV 字段包含所有可分组维度：

```text
benchmark,pattern,model,token_length,block_size,batch_size,threads,transport,memory_type,bandwidth_gbps,p99_us
```

画图脚本要求：

- Python 脚本读取 CSV/JSON 生成 PNG。
- benchmark 主程序可以在结束后自动调用画图脚本，也可以由 run script 调用。
- 如果运行环境没有 matplotlib，benchmark 不失败，只提示未生成图，但 JSON/CSV 必须正常输出。

## 六、实施阶段

### Phase 1：通信 benchmark 基础框架

- 新建 `comm_benchmark`。
- 实现统一 config、stats、memory manager。
- 实现 1:1 p2p target/initiator。
- 支持 block/batch/thread sweep。
- 支持 read/write/mix。
- 支持 warmup 和 consistency check。
- 输出 JSON/CSV/PNG。

### Phase 2：HTTP metadata 和多拓扑

- 实现 HTTP metadata server。
- 实现 HTTP metadata client。
- 支持 benchmark group。
- 支持 one-to-many。
- 支持 many-to-one。
- 提供 `run_comm_benchmark.py`。
- 复用 Phase 1 的 runner、stats、memory manager。

### Phase 3：KV benchmark

- 新建 `kv_benchmark`。
- 实现 `kvstore`。
- 实现 `SegmentManager`。
- 实现模型配置 `ModelSpec`。
- 支持 `deepseek-r1`、`kimi-k2.5`、`glm5`。
- 支持 token length 矩阵：16K、32K、64K、128K。
- 提供 `run_kv_benchmark.py`，默认启动 8 个进程，对应 8 个 device。
- 每个进程默认持有 1 个 segment，并注册 10 GiB host memory 作为 KV cache pool。
- KV benchmark 固定测试 `d2rh` / `rh2d`：`put` 写入 remote host pool，`get` 从 remote host pool 读回 device。
- 实现 `batch_put_from_multi_buffers`。
- 实现 `batch_get_into_multi_buffers`。
- 输出 KV 语义指标和折线图。

### Phase 4：FabricMem 统一接入

- 将现有 FabricMem benchmark 的内存和传输初始化逻辑抽成公共模块。
- 接入 `comm_benchmark`。
- 接入 `kv_benchmark`。
- 保证 `host`、`device` 两类 memory type 的参数路径一致。
- 使用 `--transport=fabric_mem` 控制 FabricMem 传输路径，不再引入 `fabric_mem` memory type。

### Phase 5：测试和回归

当前机器无法执行真实多机、多 device 或 FabricMem 传输测试，因此第一阶段测试目标是保证通用逻辑正确。涉及真实 HIXL 传输的路径用 stub/fake transfer runner 打桩验证流程，不把这些临时测试代码提交入库。

可验证的通用逻辑：

- config parse。
- stats percentile。
- seed 分配可复现。
- segment manager offset 分配。
- model config key size 计算。
- token length 到 key count 的换算。
- `batch_put_from_multi_buffers` / `batch_get_into_multi_buffers` 的 key placement 和请求拆分逻辑。
- CSV/JSON 输出。
- 画图脚本输入输出字段。

打桩测试：

- 1:1 p2p read/write 流程使用 fake transfer runner。
- one-to-many / many-to-one 的 metadata 和任务编排使用本地 fake worker。
- KV put/get 使用 fake transfer runner 校验 `d2rh` / `rh2d` 请求生成。
- FabricMem 只验证参数解析和路径选择，不验证真实 ACL FabricMem 调用。

构建和验证：

```bash
bash build.sh --examples
bash tests/run_test.sh -t cpp
```

如果涉及 `llm_datadist` 相关接口，也需要运行对应测试目标。

## 七、关键约束

- 1:1 和 many 拓扑通信 benchmark 必须共用核心代码。
- `kvstore` 接口模拟 Mooncake Store 语义，但不实现完整 Mooncake Store。
- `batch_put_from_multi_buffers` 和 `batch_get_into_multi_buffers` 是 `kvstore` 接口，不是 HIXL 接口。
- 模型配置必须表达为“层数 + 每层多个 cache 大小”。
- 一个 key 固定对应 128 tokens。
- KV benchmark 必须按 token length 扫描并输出折线图。
- KV benchmark 没有 target / initiator 角色，必须通过脚本统一启动，默认 8 进程、8 device、每进程 1 个 10 GiB host segment。
- KV benchmark 的真实传输类型固定为 `d2rh` / `rh2d`。
- FabricMem 是公共传输参数，不是 memory type，不再是独立 benchmark。
- 所有 benchmark 输出必须包含 JSON、CSV 和 PNG 折线图。
- 当前环境先保证通用逻辑正确；真实传输相关验证可打桩，临时测试代码不提交入库。
- 文档和实现中的必要代码注释使用英文。
