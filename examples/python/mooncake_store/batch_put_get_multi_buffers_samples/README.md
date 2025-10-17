### 样例介绍

测试Hixl对接Mooncake Store中batch_put_from_multi_buffers、batch_get_into_multi_buffers零拷贝相关接口功能

#### batch_put_from_multi_buffers

```
def batch_put_from_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int]], all_sizes: List[List[int]],
                                 config: ReplicateConfig = None) -> List[int]
```

**Parameters:**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[int]): all List of memory addresses
- `sizes` (List[int]): all List of buffer sizes
- `config` (ReplicateConfig, optional): Replication configuration

**Returns:**

- `List[int]`: List of status codes for each operation (0 = success, negative = error)

#### batch_get_into_multi_buffers

```
def batch_get_into_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int], all_sizes: List[List[int]) ->
List[int]
```

**Parameters:**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[int]): List of memory addresses
- `all_sizes` (List[int]): List of buffer sizes

**Returns:**

- `List[int]`: List of bytes read for each operation (positive = success, negative = error)

⚠️ **Buffer Registration Required**: All buffers must be registered before batch zero-copy operations.

[接口文档](https://kvcache-ai.github.io/Mooncake/python-api-reference/mooncake-store.html)

### 环境准备（已安装可跳过）

1. 安装CANN包，样例中场景为root用户安装与使用
2. Mooncake编译安装 使用`-DUSE_ASCEND_DIRECT=ON` 参数启用Hixl功能

> mkdir build && cd build \
> cmake -DUSE_ASCEND_DIRECT=ON .. \
> make -j \
> make install

### 执行测试用例

- 启动Mooncake master

```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

- 运行测试：

以单机两卡环境下进行d2d测试为例，在两个终端分别执行

```bash
bash run.sh d2d_sample.py 0
```

```bash
bash run.sh d2d_sample.py 3
```

