# CacheKey

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

It constructs `CacheKey`, which is usually used as the parameter type in the `allocate_cache` and `pull_cache` APIs of CacheManager.

## Prototype

```
__init__(*args, **kwargs)
```

## Parameters

| Parameter| Data Type| Value|
| --- | --- | --- |
| cluster_id | int | (Mandatory) ID of the remote cluster where the cache is located.|
| req_id | int | (Mandatory) Request ID associated with the cache.|
| model_id | int | ID of the model associated with the cache. The default value is `0`.|
| prefix_id | int | Public prefix ID associated with the cache. The default value is 2<sup>64</sup> – 1.|

## Example

```
from llm_datadist import CacheKey
cache_key = CacheKey(0, 1, 0)
```

## Returns

In normal cases, the CacheKey instance is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

## Constraints

None
