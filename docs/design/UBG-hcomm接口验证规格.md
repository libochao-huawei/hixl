# UBG — hcomm 验证对接清单

## 你需要做什么

UBG 是 A5 平台新增的 scale-out 协议。对 hcomm 来说只新增了一个枚举值 `COMM_PROTOCOL_UBG = 9`，建链/传输/内存管理的接口签名完全不变。**你只需验证 3 个接口在 `protocol=9` 时行为正确**，其余接口（Write/Read/Fence/BatchTransfer/Thread 等）与协议无关，不用重测。

| 接口 | UBG 带来什么变化 | 你要验证什么 |
|------|----------------|------------|
| `HcommEndpointCreate` | 新枚举值 9 + EID 寻址 | protocol=9 不报未知协议；16 字节 EID 正确解析 |
| `HcommChannelCreate` | 对端 endpoint 也是 protocol=9 + EID | 两端 EID `memcmp` 匹配后建链成功 |
| `HcommMemReg` | host 内存被 HIXL 预映射，**hcomm 收到的永远是 `type=DEVICE`** | UBG endpoint 上注册内存时 `type` 始终为 0(DEVICE)，不会收到 HOST |

---

## 测试用的具体数值

### UBG EID（16 字节）

```
本端 EID: 00 00 00 00 00 00 00 80 0a 00 00 00 00 00 00 01
对端 EID: 00 00 00 00 00 00 00 80 0a 00 00 00 00 00 00 02
```

- `byte[7] = 0x80`，校验 `(0x80 & 0xC0) == 0x80` → 是 UBG
- 对比 UBoE：`byte[7] = 0xC0`，`(0xC0 & 0xC0) == 0xC0`

### 设备信息

```
devPhyId    = 0       // 第一张卡
superDevId  = 0
serverIdx   = 0
superPodIdx = 0
```

---

## 接口 1：HcommEndpointCreate

### hcomm 收到的 `EndpointDesc`（内存布局）

```c
EndpointDesc ep = {
    .protocol  = 9,                        // COMM_PROTOCOL_UBG
    .commAddr  = {
        .type = 3,                         // COMM_ADDR_TYPE_EID
        .eid  = {0x00, 0x00, 0x00, 0x00,   // 16 字节原始 EID
                  0x00, 0x00, 0x00, 0x80,   // byte[7]=0x80 → UBG 标记
                  0x0a, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x01}
    },
    .loc = {
        .locType = 0,                      // ENDPOINT_LOC_TYPE_DEVICE
        .device = {
            .devPhyId   = 0,
            .superDevId = 0,
            .serverIdx  = 0,
            .superPodIdx = 0
        }
    },
    .raws = {0xFF, 0xFF, ...}              // 52 字节，未使用
};
```

### 你要做的

1. 检查 `endpoint->protocol == 9` 时不返回错误（旧版 hcomm 可能只认到 7=UBoE）
2. `endpoint->commAddr.type == 3`（EID）时，从 `commAddr.eid[16]` 取 16 字节地址
3. 返回有效的 `endpoint_handle`

### 验证通过的标志

```
HcommEndpointCreate(&ep, &handle) 返回 0
handle != NULL
```

---

## 接口 2：HcommChannelCreate

### hcomm 收到的参数

```c
// 入参
EndpointHandle ep_handle;     // 接口 1 返回的 handle
CommEngine engine = 2;        // COMM_ENGINE_AICPU
uint32_t channel_num = 1;

HcommChannelDesc ch_desc = {
    .header = {
        .version   = 2,
        .magicWord = 0x0fcf0f0f,
        .size      = sizeof(HcommChannelDesc),
        .reserved  = 0
    },
    .remoteEndpoint = {
        .protocol  = 9,                   // 对端也是 UBG
        .commAddr  = {
            .type = 3,                    // EID
            .eid  = {0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x80,
                      0x0a, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x02}   // 对端 EID
        },
        .loc = {
            .locType = 0,
            .device = {.devPhyId = 1, .superDevId = 0, .serverIdx = 0, .superPodIdx = 0}
        }
    },
    .notifyNum       = 1,
    .exchangeAllMems = true,
    .memHandles      = NULL,
    .memHandleNum    = 0,
    .socket          = NULL,
    .role            = 0,                 // 0=CLIENT（发起方），1=SERVER（监听方）
    .port            = 16001,
    .qos             = 0,
    // roceAttr / hccsAttr：UBG 不使用，内存值为 0xFF（memset 初始值）
};
```

### 你要做的

1. 从 `ch_desc.remoteEndpoint.commAddr.eid` 取对端 16 字节 EID
2. 用本端 endpoint 的 EID 和对端 EID 建链（建链方式应与 UBoE 共用）
3. `exchangeAllMems = true` 时自动交换双方已注册内存
4. 返回有效的 `channels[0]`

### 验证通过的标志

```
HcommChannelCreate(ep_handle, 2, &ch_desc, 1, channels) 返回 0
channels[0] != 0
```

---

## 接口 3：HcommMemReg

### 关键点：hcomm 永远不会收到 `type=HOST`

UBG endpoint 上，HIXL 在调 `HcommMemReg` 之前已经把 host 内存映射成了 device 地址。所以 **hcomm 收到的 `CommMem.type` 永远是 `0`（DEVICE）**，不管是 device 内存还是 host 内存。

#### 情况 A：注册 device 内存

```c
CommMem mem = {
    .type = 0,         // COMM_MEM_TYPE_DEVICE
    .addr = 0x1000000, // device 地址（aclrtMalloc 返回值）
    .size = 4096
};
// → hcomm 正常处理，与 RoCE/HCCS 的 device 内存注册完全一样
```

#### 情况 B：注册 host 内存（HIXL 已改写）

```c
// 用户原始：type=1(HOST), addr=0x7fff...(host 地址)
// HIXL 改写后传入 hcomm：
CommMem mem = {
    .type = 0,         // ← 注意：是 DEVICE，不是 HOST！
    .addr = 0x2000000, // ← 映射后的 device 地址（aclrtHostRegister 返回）
    .size = 4096
};
// → hcomm 按 device 内存处理即可，无需特殊逻辑
```

### 你要做的

1. UBG endpoint（protocol=9）上注册内存时，`mem->type` 应为 `0`（DEVICE）
2. **如果你在自己的代码里看到 `mem->type == 1`（HOST）+ `protocol == 9`，说明上游 HIXL 出了问题**，不是 hcomm 的 bug
3. 不需要为 UBG 做任何 host 内存的特殊处理——hcomm 看到的都是 device 内存

### 验证通过的标志

```
HcommMemReg(ep_handle, "test", &mem, &mem_handle) 返回 0
mem_handle != NULL
// 打印确认：mem->type == 0，不是 1
```

---

## 可直接编译运行的验证程序

```c
/* ubg_hcomm_verify.c — 编译: gcc -o ubg_verify ubg_hcomm_verify.c -L. -lhcomm */
#include <stdio.h>
#include <string.h>
#include "hcomm_res_defs.h"

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s\n", msg); return -1; } \
    printf("[OK]   %s\n", msg); \
} while (0)

int main(void) {
    /* ====== 本端 UBG EID ====== */
    uint8_t local_eid[16] = {0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x80,
                              0x0a,0x00,0x00,0x00, 0x00,0x00,0x00,0x01};
    /* 对端 UBG EID */
    uint8_t remote_eid[16] = {0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x80,
                               0x0a,0x00,0x00,0x00, 0x00,0x00,0x00,0x02};

    /* ====== 1. EndpointCreate ====== */
    EndpointDesc ep;
    EndpointDescInit(&ep, 1);
    ep.protocol              = COMM_PROTOCOL_UBG;         /* 9 */
    ep.commAddr.type         = COMM_ADDR_TYPE_EID;        /* 3 */
    memcpy(ep.commAddr.eid, local_eid, 16);
    ep.loc.locType           = ENDPOINT_LOC_TYPE_DEVICE;  /* 0 */
    ep.loc.device.devPhyId   = 0;
    ep.loc.device.superDevId = 0;
    ep.loc.device.serverIdx  = 0;
    ep.loc.device.superPodIdx = 0;

    EndpointHandle ep_handle = NULL;
    HcommResult ret = HcommEndpointCreate(&ep, &ep_handle);
    CHECK(ret == 0, "EndpointCreate: protocol=9 不报错");
    CHECK(ep_handle != NULL, "EndpointCreate: 返回有效 handle");

    /* ====== 2. MemReg (device) ====== */
    CommMem dev_mem = {
        .type = COMM_MEM_TYPE_DEVICE,   /* 0 */
        .addr = (void *)0x1000000,       /* mock device 地址 */
        .size = 4096
    };
    HcommMemHandle dev_mem_handle = NULL;
    ret = HcommMemReg(ep_handle, "dev_buf", &dev_mem, &dev_mem_handle);
    CHECK(ret == 0, "MemReg: device 内存注册成功");
    CHECK(dev_mem.type == COMM_MEM_TYPE_DEVICE, "MemReg: type==DEVICE(0)");

    /* ====== 2b. MemReg (host 经 HIXL 映射后，hcomm 收到的也是 DEVICE) ====== */
    CommMem mapped_mem = {
        .type = COMM_MEM_TYPE_DEVICE,   /* ← 不是 HOST！HIXL 已改写 */
        .addr = (void *)0x2000000,       /* mock 映射后 device 地址 */
        .size = 4096
    };
    HcommMemHandle host_mem_handle = NULL;
    ret = HcommMemReg(ep_handle, "host_buf_mapped", &mapped_mem, &host_mem_handle);
    CHECK(ret == 0, "MemReg: host 映射后注册成功（type==DEVICE）");
    CHECK(mapped_mem.type == COMM_MEM_TYPE_DEVICE, "MemReg: host 内存 type 也是 DEVICE");

    /* ====== 3. ChannelCreate ====== */
    /* 注意：ChannelCreate 需要两端进程（server + client），单进程跑不通。
       单进程验证时注释掉此段；双进程时 server 设 role=SERVER 监听，client 设 role=CLIENT 连接。 */
    HcommChannelDesc ch_desc;
    HcommChannelDescInit(&ch_desc, 1);
    ch_desc.remoteEndpoint.protocol    = COMM_PROTOCOL_UBG;     /* 9 */
    ch_desc.remoteEndpoint.commAddr.type = COMM_ADDR_TYPE_EID;  /* 3 */
    memcpy(ch_desc.remoteEndpoint.commAddr.eid, remote_eid, 16);
    ch_desc.remoteEndpoint.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
    ch_desc.remoteEndpoint.loc.device.devPhyId = 1;
    ch_desc.notifyNum       = 1;
    ch_desc.exchangeAllMems = true;
    ch_desc.role            = HCOMM_SOCKET_ROLE_CLIENT;  /* 0 */
    ch_desc.port            = 16001;
    ch_desc.qos             = 0;

    ChannelHandle channel = 0;
    ret = HcommChannelCreate(ep_handle, COMM_ENGINE_AICPU, &ch_desc, 1, &channel);
    CHECK(ret == 0, "ChannelCreate: UBG EID 建链成功");
    CHECK(channel != 0, "ChannelCreate: 返回有效 channel");

    /* ====== 4. 清理 ====== */
    if (channel != 0) {
        ChannelHandle ch_list[1] = {channel};
        HcommChannelDestroy(ch_list, 1);
        printf("[OK]   ChannelDestroy\n");
    }
    if (dev_mem_handle != NULL) {
        HcommMemUnreg(ep_handle, dev_mem_handle);
        printf("[OK]   MemUnreg (device)\n");
    }
    if (host_mem_handle != NULL) {
        HcommMemUnreg(ep_handle, host_mem_handle);
        printf("[OK]   MemUnreg (host mapped)\n");
    }
    if (ep_handle != NULL) {
        HcommEndpointDestroy(ep_handle);
        printf("[OK]   EndpointDestroy\n");
    }

    printf("\n=== ALL PASSED ===\n");
    return 0;
}
```

> **说明**：
> - **EndpointCreate + MemReg** 可在单进程内验证（只需 hcomm 库 + struct 填值正确即可返回成功）。
> - **ChannelCreate** 需两端进程：server 设 `role=SERVER` 先监听，client 设 `role=CLIENT` 连接。单进程跑此段会 FAIL。
> - **MemReg 的 addr**：程序中用了 mock 地址（`0x1000000`），仅验证 hcomm 对 struct 的解析。如需实际注册内存，addr 必须是 `aclrtMalloc`（device）或 `aclrtHostRegister` 映射后（host）的真实地址，否则 hcomm 底层硬件映射会失败。

---

## 如果出问题怎么排查

| 现象 | 打印什么 | 预期值 | 不对说明什么 |
|------|---------|--------|------------|
| EndpointCreate 返回错误 | `endpoint->protocol` | 9 | hcomm 不认识枚举值 9，需升级 |
| | `endpoint->commAddr.eid[7] & 0xC0` | 0x80 | EID 错误，传了 UBoE 的 EID（0xC0） |
| ChannelCreate 返回错误 | `ch_desc.remoteEndpoint.protocol` | 9 | 对端 endpoint 协议不对 |
| | `ch_desc.remoteEndpoint.commAddr.eid[7] & 0xC0` | 0x80 | 对端 EID 不是 UBG 类型 |
| | 本端 `eid[16]` 对比 对端 `eid[16]` | 两者不同（各自设备的 EID） | 相同则配错了（自己连自己） |
| MemReg 返回错误 | `mem->type` | 0 (DEVICE) | 上游 HIXL 未做 host→device 映射 |
| | `mem->addr` | 非 NULL，且为有效 device 地址 | 地址无效或映射失败 |
