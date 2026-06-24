# TransferWithCacheKeyConfig

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

It constructs a `TransferWithCacheKeyConfig` instance.

## Prototype

```
__init__(cache_key: Union[BlocksCacheKey, CacheKeyByIdAndIndex], src_layer_range: range = None, dst_layer_range: range = None, src_batch_index: int = 0)
```

## Parameters

| Parameter| Data Type| Value|
| --- | --- | --- |
| cache_key | Union[BlocksCacheKey, CacheKeyByIdAndIndex] | Cluster ID of the instance where the destination cache is located.|
| src_layer_range | range | (Mandatory) Range of layers with data to be transmitted locally. The step must be `1`.|
| dst_layer_range | range | (Mandatory) Range of layers with data to be transmitted remotely. The step must be `1`.|
| src_batch_index | int | Batch index of the local cache. This parameter can be set when the source cache is in the non-PA scenario.|

## Example

```
from llm_datadist import TransferWithCacheKeyConfig
TransferWithCacheKeyConfig(BlocksCacheKey(1), range(0, 40), range(0, 40))
```

## Returns

In normal cases, a `TransferWithCacheKeyConfig` instance is returned.

If a parameter is incorrect, `TypeError`, `ValueError`, or `LLMException` may be reported.

## Constraints

- The value of `src_layer_range` must be equal to that of `dst_layer_range`.
- When `cache_key` is set to `BlocksCacheKey`, `src_batch_index` can only be 0.
