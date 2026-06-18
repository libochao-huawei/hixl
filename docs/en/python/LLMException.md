# LLMException

The `LLMException` exception may be reported when you call `LLMDataDist` APIs. Currently, this class has only one API: `status_code`.

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Function Description

It obtains the error code of the exception. For details about the error codes, see LLMStatusCode.

## Prototype

```
status_code()
```

## Parameters

None

## Example

```
from llm_datadist import *
...
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
try:
    kv_cache_manager.pull_cache(cache_keys[0], cache, 0)
except LLMException as exe:
    print(exe.status_code)
```

## Returns

The error code is returned.

## Constraints

None
