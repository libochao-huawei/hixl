# CacheKeyByIdAndIndex

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

It constructs `CacheKeyByIdAndIndex`, which is usually used as the parameter type in the `pull_cache` and `push_cache` APIs of CacheManager.

## Prototype

```
__init__(cluster_id: int, cache_id: int, batch_index: int = 0)
```

## Parameters

| Parameter| Data Type| Value|
| --- | --- | --- |
| cluster_id | int | Cluster ID of the node where the cache is located.|
| cache_id | int | Cache ID associated with the cache.|
| batch_index | int | Batch index of the cache.|

## Example

```
from llm_datadist import CacheKeyByIdAndIndex
cache_key = CacheKeyByIdAndIndex(0, 1, 0)
```

## Returns

In normal cases, the CacheKeyByIdAndIndex instance is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

## Constraints

None
