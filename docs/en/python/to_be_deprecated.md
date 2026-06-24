# To Be Deprecated

## Product Support

| Product | Supported |
| --- | --- |
| Atlas A3 training series products/Atlas A3 inference series products | Yes |
| Atlas A2 training series products/Atlas A2 inference series products | Yes |

Note: For Atlas A2 training series products/Atlas A2 inference series products, only Atlas 800I A2 inference server/A200I A2 Box heterogeneous component.

## CacheManager

### Allocate\_cache

**Function**

Allocates Cache. After Cache is allocated successfully, it is referenced by both cache\_id and cache\_keys. The resources occupied by the cache are actually released only after all these references are released.

The cache\_id reference must be released through deallocate\_cache. The cache\_keys references can be released in either of the following two ways.

- Released after Decode successfully calls the pull\_cache or push\_cache API.
- Released when PROMPT calls the remove\_cache\_key API.

**Prototype**

```
allocate_cache(cache_desc: CacheDesc, cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = ())
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| cache_keys | Union[Tuple[CacheKey], List[CacheKey]] | Cache index. |

**Example**

```
from llm_datadist import *
...
cache_desc = CacheDesc(1, [2, 1024 * 1024], DataType.DT_FLOAT16)
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
cache = cache_manager.allocate_cache(cache_desc, cache_keys)
```

**Return Value**

Returns Cache in normal cases.

If the input data type is incorrect, TypeError or ValueError is thrown.

If cache\_keys contains a CacheKey bound during memory allocation, LLMException is thrown.

If the execution time exceeds the sync\_kv\_timeout configuration, LLMException is thrown.

**Constraints**

- When cache\_keys is passed in, if the Cache batch size is greater than 1, the same number of CacheKeys must be provided to reference a group of KV tensors separately.
- If the batch of the current inference does not fully occupy all slots, that is, an invalid batch\_index exists, insert a special CacheKey by setting req\_id to UINT64\_MAX as a placeholder. If the idle batch\_index is at the end, it can be omitted.
- If duplicate cache\_keys exist, the last one takes effect.
- Configure the memory pool before calling this API.

### Deallocate\_cache

**Function**

Release Cache.

If the Cache is associated with a CacheKey when allocated, the actual release is delayed until all CacheKeys are pulled or remove\_cache\_key is executed.

**Prototype**

```
deallocate_cache(cache: Cache)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| cache | Cache | Cache to release. |

**Example**

```
from llm_datadist import *
...
cache_manager.deallocate_cache(cache)
```

**Return Value**

In normal cases: No return value.

If the input data type is incorrect, TypeError or ValueError is thrown.

If the execution time exceeds the sync\_kv\_timeout configuration, LLMException is thrown.

**Constraints**

If the Cache does not exist or has been released, this operation is a no-op.

### Remove\_cache\_key

**Function**

Remove CacheKey.

After CacheKey is removed, the Cache can no longer be pulled by pull\_cache.

**Prototype**

```
remove_cache_key(cache_key: CacheKey)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| cache_key | CacheKey | CacheKey to remove. |

**Example**

```
from llm_datadist import *
...
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
cache_manager.remove_cache_key(cache_keys[0])
cache_manager.remove_cache_key(cache_keys[1])
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

If the execution time exceeds the sync\_kv\_timeout configuration, LLMException is thrown.

**Constraints**

If the CacheKey does not exist or has been removed, this operation is a no-op.

### Copy\_cache

**Function**

Copy Cache.

**Prototype**

```
copy_cache(dst: Cache, src: Cache, dst_batch_index: int = 0, src_batch_index: int = 0, offset: int = 0, size: int = -1, req_id: Optional[int] = None)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| dst | Cache | destination Cache. |
| src | Cache | source Cache. |
| dst_batch_index | int | batch_index of the destination Cache. The default value is 0. |
| src_batch_index | int | batch_index of the source Cache. The default value is 0. |
| offset | int | Offset of each tensor. The default value is 0. |
| size | int | Set to an integer greater than 0 to indicate the size to copy.<br>Or set to -1 to indicate full copy.<br>The default value is -1. |
| req_id | Optional[int] | req_id associated with this call. If this parameter is set, the req_id is printed in the local maintenance logs related to the call.<br>The default value is None. |

**Example**

```
from llm_datadist import *
...
cache_manager.copy_cache(dst_cache, src_cache, 0, 1, 0, 128)
```

**Return Value**

In normal cases: No return value.

If the input data type is incorrect, TypeError or ValueError is thrown.

If the execution time exceeds the sync\_kv\_timeout configuration, LLMException is thrown.

**Constraints**

The CacheDesc of the source Cache and destination Cache must match.

### Allocate\_blocks\_cache

**Function**

In PagedAttention scenarios, allocates Cache for multiple blocks. After Cache is allocated successfully, memory can be released through deallocate\_blocks\_cache.

**Prototype**

```
allocate_blocks_cache(cache_desc: CacheDesc, blocks_cache_key: Optional[BlocksCacheKey] = None)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| blocks_cache_key | Optional[BlocksCacheKey] | Indexes a blocks cache. |

**Example**

```
from llm_datadist import *
...
blocks_cache_key = BlocksCacheKey(1, 0)
blocks_cache = cache_manager.allocate_blocks_cache(cache_desc, blocks_cache_key)
```

**Return Value**

Returns Cache in normal cases.

If the input data type is incorrect, TypeError or ValueError is thrown.

If the execution time exceeds the sync\_kv\_timeout configuration, LLMException is thrown.

**Constraints**

Configure the memory pool before use.

### Deallocate\_blocks\_cache

**Function**

Allocates Cache. After Cache is allocated successfully, it is referenced by both cache\_id and cache\_keys. The resources occupied by the cache are actually released only after all these references are released.

The cache\_id reference must be released through deallocate\_cache. The cache\_keys references can be released in either of the following two ways.

- Released after Decode successfully calls the pull\_cache or push\_cache API.
- Released when PROMPT calls the remove\_cache\_key API.

**Prototype**

```
allocate_cache(cache_desc: CacheDesc, cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = ())
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| cache_keys | Union[Tuple[CacheKey], List[CacheKey]] | Cache index. |

**Example**

```
from llm_datadist import *
...
cache_desc = CacheDesc(1, [2, 1024 * 1024], DataType.DT_FLOAT16)
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
cache = cache_manager.allocate_cache(cache_desc, cache_keys)
```

**Return Value**

Returns Cache in normal cases.

If the input data type is incorrect, TypeError or ValueError is thrown.

If cache\_keys contains a CacheKey bound during memory allocation, LLMException is thrown.

If the execution time exceeds the sync\_kv\_timeout configuration, LLMException is thrown.

**Constraints**

- When cache\_keys is passed in, if the Cache batch size is greater than 1, the same number of CacheKeys must be provided to reference a group of KV tensors separately.
- If the batch of the current inference does not fully occupy all slots, that is, an invalid batch\_index exists, insert a special CacheKey by setting req\_id to UINT64\_MAX as a placeholder. If the idle batch\_index is at the end, it can be omitted.
- If duplicate cache\_keys exist, the last one takes effect.
- Configure the memory pool before calling this API.

### Copy\_blocks

**Function**

Copies blocks in PagedAttention scenarios.

**Prototype**

```
copy_blocks(cache: Cache, copy_block_info: Dict[int, List[int]])
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| cache | Cache | destination Cache. |
| copy_block_info | Dict[int, List[int]] | The content in the dict represents (source block index, destination block index list). |

**Example**

```
cache_manager.copy_blocks(cache, {1: [2,3]})
```

**Return Value**

In normal cases: No return value.

If the input data type is incorrect, TypeError or ValueError is thrown.

If the execution time exceeds the sync\_kv\_timeout configuration, LLMException is thrown.

**Constraints**

None

### Swap\_blocks

**Function**

Performs swap-in and swap-out for cpu\_cache and npu\_cache.

For swap out, this API starts four threads to execute parallel tasks. For swap in, this API starts one d2d thread. For stable performance, process CPU affinity binding is recommended.

The swap in function has two phases: H2D and D2D. To ensure performance, this API allocates a buffer of four block sizes for pipelined copy. Therefore, reserve the corresponding Device memory to prevent OOM.

**Prototype**

```
swap_blocks(src_cache: Cache, dst_cache: Cache, src_to_dst: Dict[int, int])
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| src_cache | Cache | Source Cache. |
| dst_cache | Cache | Destination Cache. |
| src_to_dst | Dict[int, int] | The content in the dict represents (source block index, destination block index). |

**Example**

```
from llm_datadist import Cache
npu_cache = cache_manager.allocate_blocks_cache(npu_cache_desc, npu_cache_key)
cpu_cache = Cache.create_cpu_cache(cpu_cache_desc, cpu_addrs) # cpu_addrs comes from the created CPU tensors
# Swap In
cache_manager.swap_blocks(cpu_cache, npu_cache, {1:2, 3:4})
# Swap Out
cache_manager.swap_blocks(npu_cache, cpu_cache, {1:2, 3:4})
```

**Return Value**

In normal cases: No return value.

If the input data type is incorrect or src and dst do not match, TypeError or ValueError is thrown.

If the input parameter is None, AttributeError is thrown.

**Constraints**

Only supports PagedAttention scenario use.

## LLMConfig

### Mem\_pool\_cfg

**Function**

Configures memory pool related configuration items.

**Prototype**

```
mem_pool_cfg(mem_pool_cfg)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| mem_pool_cfg | String | JSON array format string, including memory_size and page_shift. |

| Configuration Item | Optional/Required | Description |
| --- | --- | --- |
| memory_size | Required | Size of the current memory pool. The type is int, the value must be greater than 0, and the unit is Byte. |
| page_shift | Optional | Shift value of page_size, used to calculate page_size. During memory allocation, memory is aligned to a multiple of page_size. Set it to a proper size based on the actual scenario.<br>The type is int, and the value range is [10, 31).<br>For example, when page_shift = 16, page_size is 1<<16=65536.<br>The default value is 16. |

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.mem_pool_cfg= "{\"memory_size\": 18737418240, \"page_shift\": 16}"
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None

### Host\_mem\_pool\_cfg

**Function**

Configures host memory pool related configuration items.

**Prototype**

```
host_mem_pool_cfg(host_mem_pool_cfg)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| host_mem_pool_cfg | String | JSON array format string, including memory_size and page_shift. |

| Configuration Item | Optional/Required | Description |
| --- | --- | --- |
| memory_size | Required | Size of the current memory pool. The type is int, the value must be greater than 0, and the unit is Byte. |
| page_shift | Optional | Shift value of page_size, used to calculate page_size. During memory allocation, memory is aligned to a multiple of page_size. Set it to a proper size based on the actual scenario.<br>The type is int, and the value range is [10, 31).<br>For example, when page_shift = 16, page_size is 1<<16=65536.<br>The default value is 16. |

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.host_mem_pool_cfg= "{\"memory_size\": 18737418240, \"page_shift\": 16}"
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

The host memory pool cannot exceed 20 GB.
