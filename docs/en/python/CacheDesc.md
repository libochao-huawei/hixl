# CacheDesc

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

It constructs `CacheDesc`, which is usually used as the parameter type in the `allocate_cache` API of CacheManager.

## Prototype

```
__init__(self,
                 num_tensors: int,
                 shape: Union[Tuple[int], List[int]],
                 data_type: DataType,
                 placement: Placement = Placement.DEVICE,
                 batch_dim_index: int = 0,
                 seq_len_dim_index: int = -1,
                 kv_tensor_format: str = None)
```

## Parameters

| Parameter| Data Type| Value|
| --- | --- | --- |
| num_tensors | int | Number of tensors in the cache. When the cache is operated, all tensors perform the same operation.|
| shape | Union[Tuple[int], List[int]] | Tensor shape.|
| data_type | DataType | Data type of a tensor.|
| placement | Placement | Type of the device where the cache is located. The default value is `Placement.DEVICE`.|
| batch_dim_index | int | Dimension of the batch size in the shape. The default value is `0`, indicating dimension 0.|
| seq_len_dim_index | int | Dimension of `seq_len` in the shape. The default value is `-1` indicating that the dimension is not configured.|
| kv_tensor_format | str | Cache format. By default, this parameter is not set.|

## Example

```
from llm_datadist import CacheDesc
cache_desc = CacheDesc(80, [4, 2048, 1, 128], DataType.DT_FLOAT16)
```

## Returns

In normal cases, the CacheDesc instance is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

## Constraints

None
