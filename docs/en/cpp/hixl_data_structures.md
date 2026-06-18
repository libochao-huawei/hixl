# HIXL Data Structures

## MemDesc

Memory description.

```
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```

## MemHandle

Handle of the memory.

```
using MemHandle = void *;
```

## MemType

Memory type.

```
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```

## TransferOp

Type of the transfer operation.

```
enum TransferOp {
  READ,
  WRITE
}
```

## TransferOpDesc

Description of the transfer operation.

```
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```

## TransferArgs

Optional parameter of the transfer operation. This parameter is not used currently.

```
struct TransferArgs{
  uint8_t reserved[128] = {};
}
```

## TransferReq

Handle of the request transfer.

```
using TransferReq = void *;
```

## TransferStatus

Status of the asynchronous transfer.

```
enum class TransferStatus {
  WAITING,
  COMPLETED,
  TIMEOUT, // Not supported currently
  FAILED
}
```

## NotifyDesc

Description of the notification.

```
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
  }
```
