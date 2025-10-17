### 样例介绍

测试Hixl对接Mooncake Store中batch_put_from、batch_get_into零拷贝相关接口功能

#### batch_put_from

```
def batch_put_from(self, keys: List[str], buffer_ptrs: List[int], sizes: List[int], config: ReplicateConfig = None) -> List[int]
```

**参数:**

- `keys` (List[str]): List of object identifiers
- `buffer_ptrs` (List[int]): List of memory addresses
- `sizes` (List[int]): List of buffer sizes
- `config` (ReplicateConfig, optional): Replication configuration

**返回值:**

- `List[int]`: List of status codes for each operation (0 = success, negative = error)

#### batch_get_into

```
def batch_get_into(self, keys: List[str], buffer_ptrs: List[int], sizes: List[int]) -> List[int]
```

**参数:**

- `keys` (List[str]): List of object identifiers
- `buffer_ptrs` (List[int]): List of memory addresses
- `sizes` (List[int]): List of buffer sizes

**返回值:**

- `List[int]`: List of bytes read for each operation (positive = success, negative = error)

[接口文档](https://kvcache-ai.github.io/Mooncake/python-api-reference/mooncake-store.html)

### 环境准备（已安装可跳过）

1. 安装CANN包，样例中场景为root用户安装与使用
2. Mooncake编译安装 使用`-DUSE_ASCEND_DIRECT=ON` 参数启用Hixl功能
> mkdir build && cd build \
> cmake -DUSE_ASCEND_DIRECT=ON .. \
> make -j \
> make install

### 执行测试用例

* 启动Mooncake master
```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

* 运行测试：

以单机两卡环境下进行h2h测试为例，在两个终端分别执行
```bash
bash run.sh h2h_sample.py 0
```

```bash
bash run.sh h2h_sample.py 3
```

