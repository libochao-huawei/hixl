# HIXL数据结构

## MemDesc

内存的描述信息。

```
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```

## MemHandle

内存的Handle。

```
using MemHandle = void *;
```

## MemType

内存的类型。

```
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```

## AsyncConnectStatus

异步建链/拆链的状态。

```
enum class AsyncConnectStatus {
  NOT_CONNECT,        // 未连接
  CONNECT_PENDING,    // 建链待执行
  CONNECTING,         // 建链执行中
  CONNECTED,          // 建链成功
  CONNECT_FAILED,     // 建链失败
  DISCONNECT_PENDING, // 断链待执行
  DISCONNECTING       // 断链执行中
};
```

## TransferOp

传输操作的类型。

```
enum TransferOp {
  READ,
  WRITE
}
```

## TransferOpDesc

传输操作的描述信息。

```
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```

## TransferArgs

传输操作的可选参数，暂未启用。

```
struct TransferArgs{
  uint8_t reserved[128] = {};
}
```

## TransferReq

请求传输的Handle。

```
using TransferReq = void *;
```

## TransferStatus

异步传输的状态。

```
enum class TransferStatus {
  WAITING,
  COMPLETED,
  TIMEOUT, //暂不支持
  FAILED
}
```

## NotifyDesc

Notify的描述信息。

```
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
};
```

## EndpointConfig

本地通信资源中 `endpoint_list` 的元素描述一个可用于建链的通信 endpoint。

| 字段名 | 数据类型 | 必选/可选 | 说明 |
| ---- | ---- | ---- | ---- |
| protocol | 字符串 | 必选 | 通信协议，支持 `"roce"`、`"ub_ctp"`、`"ub_tp"`、`"uboe"`、`"ubg"` |
| comm_id | 字符串 | 必选 | 通信标识。`"roce"` 使用 IPv4/IPv6 地址；`"uboe"` 使用 device UBoE 网卡 IP；`"ub_ctp"`/`"ub_tp"`/`"ubg"` 使用 EID，其中 UBG 要求 32 位无冒号十六进制 EID |
| placement | 字符串 | 必选 | endpoint 位置，支持 `"host"` 或 `"device"`；UBG 当前使用 `"device"` |
| plane | 字符串 | 可选 | UB 平面信息，主要用于 `ub_ctp`/`ub_tp` |
| dst_eid | 字符串 | 可选 | 对端 EID，主要用于 `ub_ctp` full-mesh 直连匹配 |

示例：

```json
{
  "protocol": "ubg",
  "comm_id": "0000000000ff0ac0000000000a140200",
  "placement": "device"
}
```
