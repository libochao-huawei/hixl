# CacheTask

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## CacheTask Constructor

It constructs a CacheTask. The value is returned by the `CacheManager.transfer` API, indicating an asynchronous task for hierarchical transmission.

## synchronize

**Function**

It waits until the transfer at all layers is complete and obtains the overall execution result.

**Prototype**

```
synchronize(timeout_in_millis: Optional[int] = None) -> LLMStatusCode
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| timeout_in_millis | Optional[int] | Waiting timeout interval, in milliseconds. The default value is `None`, indicating that no timeout occurs.|

**Example**

```
ret = cache_task.synchronize()
```

**Returns**

In normal cases, LLMStatusCode is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

**Constraints**

None

## get\_results

**Function**

It waits until the transfer at all layers is complete and obtains the execution result of each TransferConfig.

**Prototype**

```
get_results(timeout_in_millis: Optional[int] = None) -> List[LLMStatusCode]
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| timeout_in_millis | Optional[int] | Waiting timeout interval, in milliseconds. The default value is `None`, indicating that no timeout occurs.|

**Example**

```
rets = cache_task.get_results()
```

**Returns**

In normal cases, the list of LLMStatusCode is returned, which corresponds to the transfer result of each TransferConfig.

If the layer corresponding to a TransferConfig has not initiated a transfer, `None` is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

**Constraints**

None
