# HIXL Benchmarks

本目录包含两个基准测试工具，分别测量 HIXL 在不同场景下的传输性能。

## 快速开始

### 1. 编译

在仓库根目录运行：

```bash
bash build.sh --examples
```

编译后的可执行文件在 `build/benchmarks/` 下：

- `comm_benchmark/hixl_comm_bench` — 通信性能测试
- `kv_benchmark/hixl_kv_bench` — KV Cache 场景测试

### 2. 一键运行全部测试

```bash
python3 benchmarks/run_all_benchmarks.py
```

这个脚本会自动：
- 通过 `npu-smi` 检测芯片型号（A2 还是 A3）
- 依次跑完所有通信方向 × 传输类型的组合
- 运行 KV Cache 基准测试（deepseek-r1、glm5 模型）
- 把结果合并到性能数据库
- 生成 `benchmarks/performance.md` 和折线统计图

跑完后打开 `benchmarks/performance.md` 即可看到结果。

### 3. 自定义参数

```bash
# 自定义测试时长和设备
python3 benchmarks/run_all_benchmarks.py --duration 10 --device_ids 0,1,2,3,4,5,6,7

# 只跑通信测试
python3 benchmarks/run_all_benchmarks.py --skip-kv

# 只跑 KV 测试
python3 benchmarks/run_all_benchmarks.py --skip-comm
```

---

## 通信 Benchmark (`hixl_comm_bench`)

测量 HIXL 在不同方向、不同传输类型下的 block 传输带宽。

**概念**：
- **Initiator**：发起传输的一方（read / write）
- **Target**：响应传输的一方（注册内存，等待 initiator 连接）
- **方向**：由 Initiator 内存类型 + Target 内存类型 + 操作类型决定

### 方向命名速查

方向名格式为 `源 → 远程目标`，其中 **D**=Device、**H**=Host、**r**=remote。

| 方向 | 含义 | 操作 |
| :--- | :--- | :---: |
| **D2rD** | Device 写往远程 Device | write |
| **rD2D** | 从远程 Device 读回 Device | read |
| **D2rH** | Device 写往远程 Host | write |
| **rH2D** | 从远程 Host 读回 Device | read |
| **H2rH** | Host 写往远程 Host | write |
| **rH2H** | 从远程 Host 读回 Host | read |
| **H2rD** | Host 写往远程 Device | write |
| **rD2H** | 从远程 Device 读回 Host | read |

### 使用脚本启动

```bash
# 快速测试一个方向
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --type=D2rD --transport=hccs

# 指定设备和端口
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --type=D2rH --transport=rdma --device_ids=0,1

# 一对多模式
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=one_to_many --num_targets=4 --type=D2rD --transport=hccs
```

脚本会自动探测 `build/benchmarks/comm_benchmark/hixl_comm_bench` 二进制，无需手动指定路径。

### 关键参数

| 参数 | 说明 | 可选值 | 默认值 |
|---|---|---|---|
| `--type` | 传输方向 | `D2rD`, `rD2D`, `D2rH`, `rH2D`, `H2rH`, `rH2H`, `H2rD`, `rD2H` | `D2rD` |
| `--transport` | 传输路径 | `hccs` / `rdma` / `fabric_mem` | `hccs` |
| `--pattern` | 通信拓扑 | `pairwise` / `one_to_many` / `many_to_one` | `pairwise` |
| `--start_block_size` | 起始 block 大小（字节） | 整数 | 16384 (16K) |
| `--max_block_size` | 最大 block 大小，2 倍递增 | 整数 | 2097152 (2M) |
| `--duration` | 每组参数测试时长（秒） | 整数 | 5 |
| `--device_ids` | 使用的设备 ID 列表 | 逗号分隔 | `0,1` |

### 传输类型与方向支持

| 平台 | HCCS | ROCE (RDMA) | FabricMem |
|---|---|---|---|
| **A2** (Ascend910B) | D2rD, rD2D | 全部 8 个方向 | 不支持 |
| **A3** (Ascend910) | D2rD, rD2D, D2rH, rH2D | 全部 8 个方向 | 全部 8 个方向 |

---

## KV Benchmark (`hixl_kv_bench`)

模拟 KV Cache 场景，按模型形状和 token 长度测试 put/get 性能。

### 模型支持

| 模型 | 层数 | Attention 类型 | KV 策略 | 每 Key 大小 |
|---|---|---|---|---|
| `deepseek-r1` | 61 | MLA | shared | 144 KiB |
| `glm5` | 78 | MLA | shared | 144 KiB |

**shared 策略**：MLA 模型所有推理 rank 共享同一份 KV Cache。测试中 rank 0 负责写入（put）全部 key，所有 rank 并行读取（get）。

### 运行示例

```bash
python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py \
  --num_processes=8 \
  --devices=0,1,2,3,4,5,6,7 \
  --segment_size=10G \
  --model=deepseek-r1 \
  --token_lengths=16K,32K,64K,128K \
  --batch_size=128 \
  --op_type=put_get \
  --transport=fabric_mem
```

### KV 参数

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--model` | 模型配置名 | `deepseek-r1` |
| `--token_lengths` | 测试的 token 长度，逗号分隔 | `16K,32K,64K,128K` |
| `--num_processes` | 并发进程数（模拟推理 rank 数） | `8` |
| `--segment_size` | 每个进程持有的 remote pool 大小 | `10G` |
| `--transport` | 传输路径 | `fabric_mem` |
| `--op_type` | 操作类型 | `put_get` |
| `--batch_size` | 批处理大小 | `128` |

---

## 性能数据

- **`performance.md`**：由 `render_performance_md.py` 从 `performance/communication_performance.json` 自动生成，**不要手动编辑表体**。
- **`performance/communication_performance.json`**：性能数据库（schema v2），以平台→方向→传输→block_size 的结构组织。
- **`performance/figures/`**：自动生成的折线图 PNG 文件。

### 数据更新流程

```bash
# 方式 1：一键完成
python3 benchmarks/run_all_benchmarks.py

# 方式 2：分步执行
# 1. 运行通信测试
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --type=D2rD --transport=hccs

# 2. 合并 CSV 到 JSON
python3 benchmarks/performance/merge_comm_csv_into_performance.py \
  --data=benchmarks/performance/communication_performance.json \
  --csv=benchmarks/comm_benchmark/output/comm_result_xxx.csv \
  --platform=atlas_a2 --direction=D2rD --transport=hccs

# 3. 刷新文档和图表
python3 benchmarks/performance/render_performance_md.py
```

---

## 目录结构

```
benchmarks/
├── README.md
├── performance.md                          # 自动生成的性能文档
├── run_all_benchmarks.py                   # 一键运行全部测试
├── performance/
│   ├── communication_performance.json      # 性能数据库 (schema v2)
│   ├── render_performance_md.py            # 渲染 MD + 图表
│   ├── merge_comm_csv_into_performance.py  # CSV → JSON 合并
│   └── figures/                            # 自动生成的折线图
├── comm_benchmark/
│   ├── hixl_comm_bench.cpp                 # 通信测试主程序
│   ├── common/
│   │   ├── benchmark_config.h/cpp          # 参数配置
│   │   ├── client_runner.cc                # Initiator 逻辑
│   │   └── server_runner.cc                # Target 逻辑
│   ├── scripts/
│   │   ├── run_comm_benchmark.py           # 启动脚本
│   │   └── plot_comm_benchmark.py          # 画图脚本
│   └── output/                             # 测试输出 (CSV/JSONL)
└── kv_benchmark/
    ├── hixl_kv_bench.cpp                   # KV 测试主程序
    ├── kvstore/
    │   ├── kvstore.h/cpp                   # KV 存储模拟
    │   ├── model_config.h/cpp              # 模型配置加载
    │   └── segment_manager.h/cpp           # 内存段管理
    ├── config/
    │   └── models.json                     # 模型参数配置
    ├── scripts/
    │   ├── run_kv_benchmark.py             # 启动脚本
    │   └── plot_kv_benchmark.py            # 画图脚本
    └── output/                             # 测试输出
```
