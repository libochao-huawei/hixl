# To Be Deprecated_ADXL Data Structures

**MemType**

Memory type.

```
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```

**TransferOp**

Type of the transfer operation.

```
enum TransferOp {
  READ,
  WRITE
}
```

**MemDesc**

Memory description.

```
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```

**TransferOpDesc**

Description of the transfer operation.

```
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```

**MemHandle**

Handle of the registered memory.

```
using MemHandle = void *;
```

**NotifyDesc**

Description of the notification.

```
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
}
```
