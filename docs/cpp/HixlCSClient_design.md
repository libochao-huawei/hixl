# HixlCSClient 类设计文档

## 概述

`HixlCSClient` 是 HIXL 通信库中的客户端核心类，负责与 HIXL Server 建立连接、注册内存、执行批量数据传输任务。

## 核心数据结构

### 1. CompleteHandle (ROCE协议)
```cpp
struct CompleteHandle {
    uint32_t magic;        // 魔数: 0x524F4345 (ROCE)
    int32_t flag_index;   // Flag队列索引
    uint64_t *flag_address; // Flag地址
};
```

### 2. UbCompleteHandle (UB协议)
```cpp
struct UbCompleteHandle {
    uint32_t magic;           // 魔数: 0x55424548 (UBEH)
    uint32_t reserved;
    CompletePool::SlotHandle slot;  // UB slot句柄
    UbBatchArgs args;
    MemDev mem_dev;
};
```

### 3. CommunicateMem (传输参数)
```cpp
struct CommunicateMem {
    uint32_t list_num;       // 传输块数量
    void **dst_buf_list;     // 目标缓冲区列表
    const void **src_buf_list; // 源缓冲区列表
    uint64_t *len_list;     // 长度列表
};
```

## 主要成员变量

### 连接相关
| 变量 | 类型 | 说明 |
|------|------|------|
| `server_ip_` | std::string | 服务器IP |
| `server_port_` | uint32_t | 服务器端口 |
| `socket_` | int32_t | TCP socket文件描述符 |
| `client_channel_` | Channel | 通道对象 |
| `client_channel_handle_` | ChannelHandle | 通道句柄 |
| `remote_endpoint_handle_` | uint64_t | 远端endpoint句柄 |
| `local_endpoint_` | EndpointPtr | 本地endpoint指针 |
| `remote_endpoint_` | EndpointDesc | 远端endpoint描述 |

### Flag队列管理 (ROCE协议)
| 变量 | 类型 | 说明 |
|------|------|------|
| `kFlagQueueSize` | static constexpr size_t | Flag队列大小: 4096 |
| `flag_queue_` | uint64_t* | Flag数组，值为0表示空闲，1表示完成 |
| `available_indices_` | std::array | 可用flag索引队列 |
| `top_index_` | size_t | 栈顶指针 |
| `live_handles_` | std::array | 记录活跃的CompleteHandle |

### UB模式相关
| 变量 | 类型 | 说明 |
|------|------|------|
| `is_ub_mode_` | bool | 是否启用UB模式 |
| `ub_device_id_` | int32_t | UB设备ID |
| `ub_remote_flag_addr_` | void* | 远端flag地址 |
| `ub_kernel_loaded_` | bool | UB kernel是否已加载 |
| `ub_notify_mem_handles_` | std::array | UB notify内存句柄 |

### 内存管理
| 变量 | 类型 | 说明 |
|------|------|------|
| `mem_store_` | HixlMemStore | 内存存储管理 |
| `tag_mem_descs_` | std::map | Tag与内存描述映射 |
| `imported_remote_bufs_` | std::vector | 已导入的远端内存 |

## 核心API

### 1. Create - 创建客户端实例
```cpp
Status Create(const char *server_ip, uint32_t server_port,
              const EndpointDesc *local_endpoint,
              const EndpointDesc *remote_endpoint,
              const HixlClientConfig *config);
```
**流程**:
1. 调用 `InitBaseClient` 初始化基础组件
2. 初始化Flag队列 (`InitFlagQueue`)
3. 调用 `InitUbResource` 初始化UB资源

### 2. Connect - 建立连接
```cpp
Status Connect(uint32_t timeout_ms);
```
**流程**:
1. 通过TCP socket连接到Server
2. 调用 `ExchangeEndpointAndCreateChannelLocked` 交换endpoint信息
3. 创建Channel通道

### 3. RegMem - 注册内存
```cpp
Status RegMem(const char *mem_tag, const HcommMem *mem, MemHandle *mem_handle);
```
**流程**:
1. 检查内存是否已注册（避免重复）
2. 调用 `local_endpoint_->RegisterMem` 注册到endpoint
3. 调用 `mem_store_.RecordMemory` 记录到本地内存存储

### 4. GetRemoteMem - 获取远端内存
```cpp
Status GetRemoteMem(HcommMem **remote_mem_list, char ***mem_tag_list,
                    uint32_t *list_num, uint32_t timeout_ms);
```
**流程**:
1. 发送获取远端内存请求
2. 接收Server返回的内存信息
3. 调用 `ImportRemoteMem` 导入内存信息到本地

### 5. BatchTransfer - 批量传输
```cpp
Status BatchTransfer(bool is_get, const CommunicateMem &communicate_mem_param,
                    void **query_handle);
```
**根据协议类型分发**:
- **ROCE协议**: 调用 `BatchTransferHost`
- **UB协议**: 调用 `BatchTransferDevice`

#### BatchTransferHost (ROCE)
1. 调用 `BatchTransferTask` 执行数据传输
2. 调用 `AcquireFlagIndex` 获取可用flag索引
3. 初始化flag为0
4. 调用 `HcommReadNbi` 异步读取flag
5. 创建 `CompleteHandle` 并返回

#### BatchTransferDevice (UB)
1. 调用 `ValidateUbInputs` 验证输入
2. 调用 `AcquireUbSlot` 获取UB slot
3. 分配设备内存 `AllocAndCopyDeviceBuffer`
4. 调用 `PrepareUbRemoteFlagAndKernel` 准备flag和kernel
5. 创建 `UbCompleteHandle` 并返回

### 6. CheckStatus - 检查传输状态
```cpp
Status CheckStatus(void *query_handle, HixlCompleteStatus *status);
```
**流程**:
1. 根据 `magic` 判断协议类型
2. **ROCE**: 调用 `CheckStatusHost`
   - 读取flag地址的值
   - 若值为1（完成），重置为0，调用 `ReleaseCompleteHandle` 释放资源
3. **UB**: 调用 `CheckStatusDevice`
   - 读取host_flag的值
   - 若值为1（完成），重置为0，调用 `ReleaseUbCompleteHandle` 释放资源

### 7. UnRegMem - 注销内存
```cpp
Status UnRegMem(MemHandle mem_handle);
```
从本地内存存储中移除记录。

### 8. Destroy - 销毁客户端
```cpp
Status Destroy();
```
**流程**:
1. 清理活跃的 `CompleteHandle`
2. 释放Flag队列内存
3. 调用 `ClearRemoteMemInfo` 清理远端内存信息
4. 关闭TCP socket

## Flag管理机制

### 初始化
- `InitFlagQueue()` 分配 `flag_queue_` (4096个uint64_t)
- 初始化 `available_indices_` 为 `[0, 1, 2, ..., 4095]`
- 初始 `top_index_ = kFlagQueueSize` (4096)

### 获取Flag
```cpp
int32_t AcquireFlagIndex() {
    if (top_index_ == 0) return -1;  // 无可用
    return available_indices_[--top_index_];
}
```

### 释放Flag (正常流程)
```cpp
Status ReleaseCompleteHandle(CompleteHandle *query_handle) {
    if (top_index_ < kFlagQueueSize) {
        available_indices_[++top_index_] = query_handle->flag_index;
        live_handles_[query_handle->flag_index] = nullptr;
    }
    delete query_handle;
}
```

### 异常场景释放
```cpp
void ReleaseFlagIndex(int32_t flag_index) {
    if (top_index_ < kFlagQueueSize) {
        available_indices_[++top_index_] = flag_index;
        flag_queue_[flag_index] = 0;
    }
}
```
在以下场景调用:
- `HcommReadNbi` 失败时
- `new CompleteHandle` 失败时

## 支持的传输模式

| 协议 | 位置 | 传输函数 |
|------|------|----------|
| ROCE | HOST | BatchTransferHost |
| ROCE | DEVICE | BatchTransferHost |
| UB_CTP | DEVICE | BatchTransferDevice |
| UB_TP | DEVICE | BatchTransferDevice |
| UB_CTP | HOST | BatchTransferHost |
| UB_TP | HOST | BatchTransferHost |

## 内存验证机制

`HixlMemStore` 类负责验证内存访问合法性:
- `RecordMemory`: 记录已注册的内存区域
- `CheckMemoryForRegister`: 检查是否重复注册
- `ValidateMemoryAccess`: 验证访问地址是否在已注册范围内
- `CheckMergedRegionsAccess`: 检查连续内存区域合并后的访问合法性

## 线程安全性

- 使用 `mutex_` 保护主要操作
- 使用 `indices_mutex_` 保护Flag索引操作
- 使用 `ub_mu_` 保护UB模式操作

## 注意事项

1. **Flag索引管理**: 确保每个传输任务都有对应的flag索引，且在任务完成后正确回收
2. **内存注册**: 必须在传输前注册内存，否则 `ValidateAddress` 会失败
3. **UB模式**: 需要提前加载kernel (`EnsureUbKernelLoadedLocked`)
4. **异常处理**: 任何分配操作失败都需要释放已获取的资源