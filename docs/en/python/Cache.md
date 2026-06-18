# Cache

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## Cache Constructor

It constructs a cache. This API does not need to be called by users. The cache object is returned by `allocate_cache`, `allocate_blocks_cache`, `register_cache`, or `register_blocks_cache` in CacheManager.

## cache_id

**Function**

It obtains the cache ID.

**Prototype**

```
@property
cache_id() -> int
```

**Parameters**

None

**Example**

```
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.cache_id)
```

**Returns**

In normal cases, a cache ID is returned.

**Constraints**

None

## cache\_desc

**Function**

It obtains the cache description.

**Prototype**

```
@property
cache_desc() -> CacheDesc
```

**Parameters**

None

**Example**

```
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.cache_desc.num_tensors)
```

**Returns**

In normal cases, cache description is returned.

**Constraints**

None

## tensor\_addrs

**Function**

It obtains the cache address.

**Prototype**

```
@property
tensor_addrs() -> List[int]
```

**Parameters**

None

**Example**

```
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.tensor_addrs)
```

**Returns**

In normal cases, the cache address is returned.

**Constraints**

None

## create\_cpu\_cache

**Function**

It creates a CPU cache.

**Prototype**

```
create_cpu_cache(cache_desc: CacheDesc, addrs: List[int])
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description.|
| addrs | List[int] | Address of the CPU cache.|

**Example**

```
from llm_datadist import Cache
cpu_cache = Cache.create_cpu_cache(cpu_cache_desc, cpu_addrs) # Obtain cpu_addrs from the created CPU tensors.
```

**Returns**

In normal cases, the return value is `cpu_cache` of the `Cache` type.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

If the input parameter is `None`, the `AttributeError` exception is reported.

**Constraints**

None
