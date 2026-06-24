# TransferConfig

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

Constructs TransferConfig.

## Prototype

```
__init__(dst_cluster_id: int, dst_addrs: List[int], src_layer_range: Optional[range] = None, src_batch_index: int = 0)
```

## Parameters

| Parameter| Data Type| Value|
| --- | --- | --- |
| dst_cluster_id | int | Cluster ID of the instance where the destination cache is located.|
| dst_addrs | List[int] | Memory address of each tensor in the destination cache. If the destination cache is not in the PA scenario and **batch_index** to be transferred is not **0**, **dst_addrs** needs to be offset to the actual address.|
| src_layer_range | Optional[range] | Range of layers with data to be transmitted locally. The step can only be **1**. The default value is **None**, indicating that the data at all layers is transmitted.|
| src_batch_index | int | Batch index of the local cache. This parameter can be set when the source cache is in the non-PA scenario.|

## Sample

```
from llm_datadist import TransferConfig
TransferConfig(1, dst_addrs, range(0, 3), 1)
```

## Return Value

In normal cases, the TransferConfig instance is returned.

If a parameter is incorrect, `TypeError` or `ValueError` may be reported.

If src\_layer\_range is invalid, LLMException is thrown.

## Constraints

The number of addresses in the destination address list must be twice the number of layers with data to be transmitted.
