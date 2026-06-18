# MemInfo

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

It constructs `MemInfo`, which is usually used as the parameter type in the `remap_registered_memory` API of CacheManager.

## Prototype

```
__init__(mem_type: Memtype, addr: int, size: int)
```

## Parameters

| Parameter| Data Type| Value|
| --- | --- | --- |
| mem_type | MemType | Memory address type.|
| addr | int | Memory address.|
| size | int | Size of the memory address, in bytes.|

## Example

```
from llm_datadist import MemInfo
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, 1234, 10)
```

## Returns

In normal cases, the MemInfo instance is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

## Constraints

None
