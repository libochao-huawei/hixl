# Bandwidth Benchmark 使用说明

## 功能概述

`benchmark_bandwidth.py` 是一个用于测试不同 block 大小下的数据传输带宽的 benchmark 工具。它支持多种传输模式，可以帮助分析 Hixl 与 Mooncake Store 集成的性能表现。

## 传输模式

该 benchmark 支持三种传输模式：

### 1. pairwise（成对传输）
- 每个 rank 从下一个 rank 获取数据（rank i 从 rank i+1 获取）
- 适用于简单的点对点性能测试
- 适合单卡或多卡环境

### 2. full_mesh（全互联传输）
- 所有 rank 相互之间都进行数据传输
- 每个 rank 向其他所有 rank put 数据，并从其他所有 rank get 数据
- 适用于测试复杂的分布式通信场景

### 3. one_to_many（一对多传输）
- rank 0 向其他所有 rank put 数据
- 其他 rank 从 rank 0 get 数据
- 适用于测试广播场景的性能

## 使用方法

### 1. 启动 Mooncake Master

首先需要启动 Mooncake master：

```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

### 2. 配置传输方式

在 `run.sh` 中设置环境变量选择传输方式：

```bash
# RDMA 传输
export HCCL_INTRA_ROCE_ENABLE=1

# PCIe 传输
export HCCL_INTRA_PCIE_ENABLE=1
```

### 3. 运行 Benchmark

使用 `run.sh` 脚本运行 benchmark：

```bash
bash run.sh benchmark_bandwidth.py [参数]
```

## 参数说明

### 基本参数

- `--device_id`: （必填）当前进程所在的 NPU 设备 ID
- `--schema`: （可选）传输模式，默认为 "d2d"，可选值：h2h, h2d, d2h, d2d
- `--transfer_mode`: （可选）传输模式，默认为 "pairwise"，可选值：
  - `pairwise`: 成对传输
  - `full_mesh`: 全互联传输
  - `one_to_many`: 一对多传输

### Benchmark 参数

- `--block_sizes`: （可选）要测试的 block 大小列表（KB），默认为 "1,4,16,64,144,256,512,1024"
- `--num_blocks`: （可选）每次迭代的 block 数量，默认为 100
- `--num_iters`: （可选）每个 block 大小的迭代次数，默认为 10

### 分布式参数

- `--config`: （可选）YAML 配置文件路径
- `--rank`: （可选）当前进程的 rank，默认为 device_id // 2
- `--world_size`: （可选）分布式集群的设备数
- `--distributed`: （可选）启用分布式模式

## 使用示例

### 示例 1：单卡测试 pairwise 模式

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise"
```

### 示例 2：测试特定 block 大小

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise" \
  --block_sizes="64,144,256" \
  --num_blocks=50 \
  --num_iters=20
```

### 示例 3：多卡 full_mesh 测试

在多卡环境中，需要在每个设备上运行：

```bash
# 在 device 0 上
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=0 \
  --world_size=4 \
  --distributed

# 在 device 1 上
bash run.sh benchmark_bandwidth.py \
  --device_id=1 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=1 \
  --world_size=4 \
  --distributed

# 在 device 2 上
bash run.sh benchmark_bandwidth.py \
  --device_id=2 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=2 \
  --world_size=4 \
  --distributed

# 在 device 3 上
bash run.sh benchmark_bandwidth.py \
  --device_id=3 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=3 \
  --world_size=4 \
  --distributed
```

### 示例 4：使用配置文件

创建配置文件 `benchmark_config.yaml`：

```yaml
distributed:
  enabled: true
  world_size: 4
  master_addr: "127.0.0.1"
  master_port: "29500"

mooncake:
  store_ip: "127.0.0.1"
  port_start: 12345
  metadata_url: "http://127.0.0.1:8080/metadata"
  grpc_url: "127.0.0.1:50051"
```

然后使用配置文件运行：

```bash
bash runr.sh benchmark_bandwidth.py \
  --device_id=0 \
  --config=benchmark_config.yaml \
  --transfer_mode="full_mesh"
```

## 输出说明

### 单个 Block Size 的输出

```
================================================================================
Benchmark Configuration:
  Block Size: 144 KB
  Number of Blocks: 100
  Number of Iterations: 10
  Transfer Mode: pairwise
  Schema: d2d
  World Size: 1
================================================================================

================================================================================
Benchmark Results for Block Size 144 KB:
  Total Time: 2.345 seconds
  Total Data Transferred: 0.137 GB
  Average Bandwidth: 0.059 GB/s
================================================================================
```

### 汇总输出

```
================================================================================
SUMMARY - Bandwidth vs Block Size:
Transfer Mode: pairwise
World Size: 1
Schema: d2d
================================================================================
Block Size (KB)      Bandwidth (GB/s)    
----------------------------------------
1                    0.010               
4                    0.035               
16                   0.120               
64                   0.450               
144                  0.059               
256                  0.980               
512                  1.850               
1024                 3.200               
================================================================================
Benchmark completed successfully!
================================================================================
```

## 性能分析建议

1. **Block Size 影响**：
   - 小 block size 可能导致频繁的传输开销
   - 大 block size 可以更好地利用带宽
   - 测试不同 block size 可以找到最优的传输块大小

2. **传输模式选择**：
   - `pairwise`: 适合点对点通信场景
   - `full_mesh`: 适合 all-to-all 通信场景，但网络开销大
   - `one_to_many`: 适合广播场景，适合测试主从架构

3. **迭代次数**：
   - 增加迭代次数可以获得更稳定的平均值
   - 建议至少运行 10 次迭代以获得可靠结果

4. **分布式环境**：
   - 在分布式环境中，所有 rank 的结果只有 rank 0 会输出汇总
   - 确保所有 rank 都正确启动并同步

## 注意事项

1. 在调用零拷贝接口前，必须完成 buffer 的注册
2. 确保 Mooncake master 正在运行
3. 不要同时禁用 ROCE 和 PCIE
4. 对于 `full_mesh` 和 `one_to_many` 模式，建议使用分布式环境
5. 确保有足够的内存来分配测试 buffer

## 故障排查

### 问题：Buffer 注册失败
- 检查内存是否足够
- 检查 buffer 大小是否超过限制

### 问题：数据传输失败
- 检查 Mooncake master 是否正常运行
- 检查网络连接
- 检查 RDMA/PCIE 配置

### 问题：分布式性能异常
- 检查所有 rank 是否正确启动
- 检查网络拓扑和延迟
- 检查是否有网络拥塞