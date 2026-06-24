# CacheManager

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported. For Ascend 950PR/Ascend 950DT, `remap_registered_memory` is not supported.

## CacheManager Constructor

**Function**

The CacheManager instance should be returned through `cache_manager`.

## pull_cache

**Function**

It obtains the cache from the peer node to the local cache based on `CacheKey`.

**Prototype**

```
pull_cache(cache_key: Union[CacheKey, CacheKeyByIdAndIndex],
           cache: Cache,
           batch_index: int = 0,
           size: int = -1, **kwargs)
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| cache_key | Union[CacheKey,CacheKeyByIdAndIndex] | `CacheKey` to be pulled.<br>Pass `CacheKey` to obtain the value by `req_id`, `prefix_id`, or `model_id`.<br>Pass `CacheKeyByIdAndIndex` to obtain the value by `cache_id` or `batch_index`.|
| cache | Cache | Target cache.|
| batch_index | int | Batch index. The default value is `0`.|
| size | int | If this parameter is set to an integer greater than `0`, it indicates the size of the tensor to be pulled.<br>If this parameter is set to `-1`, it indicates a complete copy, where the actual size is the size of a single local KV entry.<br>The default value is `-1`.|
| **kwargs | NA | This is the typical way to handle extensible parameters in Python functions: parameters are passed using the key=value format.<br>For details about optional parameters, see Table 1.|

**Table 1** Optional parameters of **kwargs

| Parameter| Data Type| Value|
| --- | --- | --- |
| src_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. It indicates the layer range of the transmission source. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| dst_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. It indicates the layer range of the transmission target. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| tensor_num_per_layer | Optional[int] | (Optional) Number of tensors at each layer. The default value is `2`. The value range is [1, Total number of tensors in the cache]. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|

**Example**

For details, see [Sample Running](../../../examples/python/README_en.md).

**Returns**

In normal cases, no value is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

If the execution time exceeds the value of `sync_kv_timeout`, an LLMException is thrown.

If the `layer_range` parameter is abnormal, an LLMException is thrown.

**Constraints**

- When `enable_remote_cache_accessible` is enabled, the `cache_key` type must be `CacheKeyByIdAndIndex`.
- In D2H and H2D transmission scenarios, the device memory pool must be configured during host initialization, with the memory pool size set to at least 100 MB.

## pull_blocks

**Function**

In the PagedAttention scenario, the cache is pulled from the peer node to the local cache based on `BlocksCacheKey` through the block list.

**Prototype**

```
pull_blocks(src_cache_key: Union[CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey],
            dst_cache: Cache,
            src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
            dst_blocks: Union[Tuple[int], List[int]] = (), **kwargs)
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| src_cache_key | Union[CacheKey,CacheKeyByIdAndIndex, BlocksCacheKey] | Remote cache index.|
| dst_cache | Cache | Target cache.|
| src_blocks | Optional[Union[Tuple[int], List[int]]] | Remote block index list. If `src_cache_key` is not `BlocksCacheKey`, leave this parameter blank.|
| dst_blocks | Union[Tuple[int], List[int]] | Local block index list.|
| **kwargs | NA | This is the typical way to handle extensible parameters in Python functions: parameters are passed using the key=value format.<br>For details about optional parameters, see Table 1.|

**Table 1** Optional parameters of **kwargs

| Parameter| Data Type| Value|
| --- | --- | --- |
| src_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. It indicates the layer range of the transmission source. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| dst_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. It indicates the layer range of the transmission target. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| tensor_num_per_layer | Optional[int] | (Optional) Number of tensors at each layer. The default value is `2`. The value range is [1, Total number of tensors in the cache]. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|

**Example**

For details, see [Sample Running](../../../examples/python/README_en.md).

**Returns**

- In normal cases, no value is returned.
- If the input data type is incorrect, `TypeError` or `ValueError` may be reported.
- If the execution time exceeds the value of `sync_kv_timeout`, an LLMException is thrown.
- If the `layer_range` parameter is abnormal, an LLMException is thrown.

**Constraints**

- When `src_cache` is a host device and `dst_cache` is a device, only the scenario where both `src_cache` and `dst_cache` are `block caches` is supported.
- Enabling `enable_remote_cache_accessible` introduces additional restrictions. For details, see the restrictions description for this function.
- In D2H and H2D transmission scenarios, the device memory pool must be configured during host initialization, with the memory pool size set to at least 100 MB.

## register_cache

**Function**

Calling this API in the non-PagedAttention scenario will register a self-allocated memory.

**Prototype**

```
register_cache(cache_desc: CacheDesc, addrs: List[int], cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = (), remote_accessible: Optional[bool] = None) -> Cache:
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description.|
| addrs | List[int] | Cache address. The total number of addresses in `register_cache` and `register_blocks_cache` cannot exceed 240.|
| cache_keys | Union[Tuple[CacheKey], List[CacheKey]] | Cache index.|
| remote_accessible | Optional[bool] | Whether the registered memory can be used for network transmission. The default value is `True` for device memory and `False` for host memory.|

**Example**

For details, see [Sample Running](../../../examples/python/README_en.md).

**Returns**

In normal cases, the registered cache is returned.

If the input data type is incorrect and the `src` and `dst` do not match, a `TypeError` or `ValueError` exception may be reported.

If the input parameter is `None`, an `AttributeError` exception will be thrown.

**Constraints**

- The device memory must be registered before link establishment, while the host memory has no restrictions on the order of operations.

- The registered memory address must be unique.

- If the HDK version is earlier than 25.5.0, a maximum of 20 GB host memory can be registered. If the HDK version is 25.5.0 or later, a maximum of 1 TB host memory can be registered. The larger the registered memory is, the more the OS memory is occupied. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products

- For D2D data transmission over HCCS, the start address must be aligned to 2 MB; otherwise, the link may fail. HCCS does not support host memory. The following chips are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products

- For RDMA data transmission, the host memory must be allocated by calling `aclrtMallocHost`. Otherwise, the link may fail. The following chips are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products

## register_blocks_cache

**Function**

Calling this API in the PagedAttention scenario will register a self-allocated memory.

**Prototype**

```
register_blocks_cache(cache_desc: CacheDesc, addrs: List[int], blocks_cache_key: Optional[BlocksCacheKey] = None, remote_accessible: Optional[bool] = None) -> Cache:
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description.|
| addrs | List[int] | Cache address. The total number of addresses in `register_cache` and `register_blocks_cache` cannot exceed 240.|
| blocks_cache_key | Optional[BlocksCacheKey] | Optional BlocksCacheKey index.|
| remote_accessible | Optional[bool] | Whether the registered memory can be used for network transmission. The default value is `True` for device memory and `False` for host memory.|

**Example**

For details, see [Sample Running](../../../examples/python/README.md).

**Returns**

In normal cases, the registered cache is returned.

If the input data type is incorrect and the data types do not match, a `TypeError` or `ValueError` exception may be reported.

If the input parameter is `None`, an `AttributeError` exception will be thrown.

**Constraints**

For data transmission over HCCS, if the memory is not allocated via `aclrtMalloc`, the address must be aligned to the page size (2 MB alignment is recommended). Otherwise, the link may fail. The following chips are supported:

- Atlas A2 training products and Atlas A2 inference products
- Atlas A3 training products and Atlas A3 inference products

In the Ascend 950PR/Ascend 950DT scenario, there is no restriction.

## transfer_cache_async

**Function**

It transfers the data of cache in asynchronously hierarchical mode.

**Prototype**

```
transfer_cache_async(self,
                     src_cache: Cache,
                     layer_synchronizer: LayerSynchronizer,
                     transfer_configs: Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]], Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]],
                     src_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_memory_size: Optional[int] = None) -> CacheTask
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| src_cache | Cache | Source cache.|
| layer_synchronizer | LayerSynchronizer | LayerSynchronizer implementation class object.|
| transfer_configs | Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]], Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]] | Transmission configuration list or tuple.|
| src_block_indices | Optional[Union[List[int], Tuple[int]]] | Block indices of the source cache, which are set when the source cache is in the PagedAttention scenario.|
| dst_block_indices | Optional[Union[List[int], Tuple[int]]] | Block indices of the destination cache, which are set when the destination cache is in the PagedAttention scenario.|
| dst_block_memory_size | Optional[int] | Size of the memory occupied by each block in the destination cache, which is set when the destination cache is in the PA scenario. If the source cache is also in the PA scenario, this parameter can be omitted. In this case, it is automatically set to the memory size occupied by each block of the source cache.<br>This parameter is omitted if the value is set to `0`.|

**Example**

```
cache_task = cache_manager.transfer_cache_async(cache, LayerSynchronizerImpl(), transfer_configs)
```

**Returns**

In normal cases, `CacheTask` is returned.

If the input data type is incorrect, a `TypeError` or `ValueError` exception may be reported.

If the input data is invalid, an `LLMException` exception may be reported.

**Constraints**

- The scenario where `src_cache` is `HOST` and `dst_cache` is `DEVICE` is not supported.
- The scenario where `src_cache` is PA and `dst_cache` is non-PA is not supported.
- The single-process multi-device scenario is not supported.
- If `dst_cache` is `HOST`, it can only be allocated through `allocate_cache`.
- Ensure the validity of `dst_addrs` in `transfer_config` and the validity of data when transferring `dst_block_indices`. Otherwise, an unknown error may occur.
- When `enable_remote_cache_accessible` is enabled, the type in `transfer_configs` must be `TransferWithCacheKeyConfig`. When `enable_remote_cache_accessible` is disabled, the type in `transfer_configs` must be `TransferConfig`.

## push\_blocks

**Function**

It pushes the cache from the local node to the remote cache via a block list based on `BlocksCacheKey` in the PagedAttention scenario.

**Prototype**

```
push_blocks(self,
                    dst_cache_key: BlocksCacheKey,
                    src_cache: Cache,
                    src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
                    dst_blocks: Union[Tuple[int], List[int]] = (),
                    src_layer_range: range = None,
                    dst_layer_range: range = None,
                    tensor_num_per_layer = _NUM_TENSORS_PER_LAYER)
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| dst_cache_key | BlocksCacheKey | Remote cache index.|
| src_cache | Cache | Local cache.|
| src_blocks | Optional[Union[Tuple[int], List[int]]] | Local block index list.|
| dst_blocks | Union[Tuple[int], List[int]] | Remote block index list.|
| src_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. The default value is `None`.<br>It indicates the layer range of the transmission source. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| dst_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. The default value is `None`.<br>It indicates the layer range of the transmission target. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| tensor_num_per_layer | Optional[int] | (Optional) Number of tensors at each layer. The default value is `2`. The value range is [1, Total number of tensors in the cache]. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|

**Example**

For details, see [Sample Running](../../../examples/python/README_en.md).

**Returns**

- In normal cases, no value is returned.
- If the input data type is incorrect, `TypeError` or `ValueError` may be reported.
- If the execution time exceeds the value of `sync_kv_timeout`, an LLMException is thrown.
- If the `layer_range` parameter is abnormal, an LLMException is thrown.

**Constraints**

- When `src_cache` is a host device and `dst_cache` is a device, only the scenario where both `src_cache` and `dst_cache` are PA is supported.
- Enabling `enable_remote_cache_accessible` introduces additional restrictions. For details, see the restrictions description for this function.
- In D2H and H2D transmission scenarios, the device memory pool needs to be configured during host initialization.
- The values of `src_layer_range` and `dst_layer_range` must be the same.

## push_cache

**Function**

It pushes the cache from the local node to the remote cache based on `CacheKey`.

**Prototype**

```
push_cache(self,
                   dst_cache_key: CacheKeyByIdAndIndex,
                   src_cache: Cache,
                   src_batch_index: int = 0,
                   src_layer_range: range = None,
                   dst_layer_range: range = None,
                   size: int = -1,
                   tensor_num_per_layer = _NUM_TENSORS_PER_LAYER) -> None
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| dst_cache_key | CacheKeyByIdAndIndex | Remote cache index.|
| src_cache | Cache | Local cache.|
| src_batch_index | int | Local batch index. The default value is `0`.|
| src_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. The default value is `None`.<br>It indicates the layer range of the transmission source. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| dst_layer_range | Optional[range] | (Optional) Used for pulling KVs by layer. The default value is `None`.<br>It indicates the layer range of the transmission target. The step can only be `1`. If this parameter is not set, data at all layers is transmitted. Note that this is the index of the layer, not the index of the tensor. That is, one layer corresponds to *N* consecutive tensors (K/V). If memory allocation is required, the tensors must be arranged in KV order. Other arrangements are not supported. *N* is the value of `tensor_num_per_layer`, which defaults to `2`.|
| tensor_num_per_layer | Optional[int] | (Optional) Number of tensors at each layer. The default value is `2`. The value range is [1, Total number of tensors in the cache]. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|
| size | int | Size of a tensor to be transferred. The default value `-1` indicates the size of a single local KV. Other values are not supported currently.|

**Example**

For details, see [Sample Running](../../../examples/python/README_en.md).

**Returns**

In normal cases, no value is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

If the execution time exceeds the value of `sync_kv_timeout`, an LLMException is thrown.

If the `layer_range` parameter is abnormal, an LLMException is thrown.

**Constraints**

- When `src_cache` is a host device and `dst_cache` is a device, only the scenario where both `src_cache` and `dst_cache` are contiguous caches is supported.
- When `enable_remote_cache_accessible` is enabled, the `cache_key` type must be `CacheKeyByIdAndIndex`.
- In D2H and H2D transmission scenarios, the device memory pool needs to be configured during host initialization.
- The values of `src_layer_range` and `dst_layer_range` must be the same.

## remap_registered_memory

**Function**

During model inference, if a memory UCE occurs, the error code `ACL_ERROR_RT_DEVICE_MEM_ERROR` is returned. The upper-layer framework needs to check whether the faulty memory is the KV cache memory. If not, obtain and fix the incorrect virtual address of the memory UCE by referring to the description of the `torch_npu.npu.restart_device` API in [PyTorch](https://www.hiascend.com/en/developer/software/ai-frameworks/pytorch). If the memory is the KV cache memory, call this API to fix the KV cache memory registered with the NIC.

>![](public_sys-resources/icon-note.gif) **Note:**
>This API is reserved and is not supported currently.

**Prototype**

```
remap_registered_memory(mem_infos: Union[MemInfo, list[MemInfo]]) -> None
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| mem_infos | Union[MemInfo, list[MemInfo]] | Memory information or memory information list.|

**Example**

```
from llm_datadist import *
role = LLMRole.PROMPT  # Role where the fault occurred
cluster_id = 1 # ID of the faulty LLMDataDist
datadist = LLMDataDist(role, cluster_id)
cache_manager = datadist.cache_manager
addr = 12345678 # Address where the fault occurs
size = 1024 # Size of the address where the fault occurs
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, addr, size)
cache_manager.remap_registered_memory(mem_info)
```

**Returns**

In normal cases, no value is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.<br><br>Ascend 950PR and Ascend 950DT do not support this API.

**Constraints**

Currently, only device memory can be repaired.

## unregister_cache

**Function**

It deregisters a self-allocated memory.

**Prototype**

```
unregister_cache(cache_id: int) -> None
```

**Parameters**

| Parameter| Data Type| Value|
| --- | --- | --- |
| cache_id | int | ID of the cache returned by calling `register_cache` or `register_blocks_cache`.|

**Example**

For details, see [Sample Running](../../../examples/python/README_en.md).

**Returns**

In normal cases, no value is returned.

If the input data type is incorrect, `TypeError` or `ValueError` may be reported.

**Constraints**

- Currently, this API is supported only in the `local_comm_res` scenario.
- Before calling this API, you must call `unlink` to disconnect the link; otherwise, HCCL will report an error.
