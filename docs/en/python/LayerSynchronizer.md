# LayerSynchronizer

Abstract class. Users need to inherit this abstract class to implement related APIs. Currently, this class has only one API: `synchronize_layer`.

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

It waits until the execution of the specified layer of the model is complete. You need to inherit `LayerSynchronizer` and implement this API.

This API is called during the execution of `transfer_cache_async`. When the API returns a success status, the cache transmission for the current layer begins.

## Prototype

```
synchronize_layer(layer_index: int, timeout_in_millis: Optional[int]) -> bool
```

## Parameters

| Parameter| Data Type| Value|
| --- | --- | --- |
| layer_index | int | Index of a layer.|
| timeout_in_millis | Optional[int] | Timeout interval. Not supported currently.|

## Example

This API is not directly called by users. Instead, it is called by CacheManager as a callback function.

## Returns

In normal cases, a message is returned, indicating whether the synchronization is successful.

If a parameter is incorrect, `TypeError` or `ValueError` may be reported.

## Constraints

None
