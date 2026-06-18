# LLM-DataDist APIs

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## LLM-DataDist Constructor

**Function**

It creates an LLM-DataDist object.

**Prototype**

```
LlmDataDist(uint64_t cluster_id, LlmRole role)
```

 **Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| cluster_id | Input| Cluster ID. It identifies the LLM-DataDist instance. It must be unique among all participants in the link establishment.|
| role | Input| The type is `LlmRole`. This parameter only identifies the current role and does not affect the transmission process.|

**Returns**

None

**Troubleshooting**

None

**Constraints**

None

## \~LlmDataDist\(\)

**Function**

It destructs an LLM-DataDist object.

**Prototype**

```
~LlmDataDist()
```

**Parameters**

None

**Returns**

None

**Troubleshooting**

None

**Constraints**

None

## Initialize

**Function**

It initializes LLM-DataDist.

**Prototype**

```
Status Initialize(const std::map<AscendString, AscendString> &options)
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| options | Input| Initialization parameter value. For details, see Table 1.|

**Table 1** options

| Parameter| Mandatory| Description|
| --- | --- | --- |
| OPTION_LISTEN_IP_INFO | No| If configured, the LLM-DataDist instance acts as a server; otherwise, it acts as a client.<br>When the LLM-DataDist instance acts as a server, the host IP address and port number must be provided.<br>Example: `192.168.1.1:26000`. Do not pass multiple IP addresses and port numbers.|
| OPTION_DEVICE_ID | Yes| Device ID of the current process, for example, `0`. Single-process multi-device scenarios are not supported.|
| OPTION_SYNC_CACHE_WAIT_TIME | No| Timeout interval of KV operations, in milliseconds. If the parameter is not configured, it defaults to `1000` ms. The related APIs are as follows:<br><br>  - PullKvCache<br>  - PullKvBlocks<br>  - PushKvCache<br>  - PushKvBlocks |
| OPTION_LOCAL_COMM_RES | No| Local communication resource information, formatted as a JSON string. The configuration method is as follows:<br>Only the device information used by the current LLM-DataDist instance in `ranktable` needs to be configured; the `server_count` and `rank_id` fields are not required. This option can be left unconfigured or set to an empty string. If left empty, the relevant information is automatically generated. This method applies to the following products:<br><br>- Atlas A2 training products and Atlas A2 inference products<br><br>- Atlas A3 training products and Atlas A3 inference products<br><br>In the Ascend 950PR/Ascend 950DT scenario, refer to [GitCode](https://gitcode.com/cann/hixl/issues/38) for the configuration format. In addition, set `OPTION_TRANSFER_BACKEND` to `hixl`. This option is mandatory. If it is left empty, related information will not be automatically generated.|
| OPTION_TRANSFER_BACKEND | No| The transmission backend engine used by LLM-DataDist. Currently, the supported backend is `hixl`. The method of using the `hixl` transmission backend is as follows:<br><br>- When initializing the option, you need to specify `OPTION_LISTEN_IP_INFO`. When the `hixl` transmission backend is used, each transmission end can function as both a client and a server.<br><br>- Before initiating transmission with the peer end, you need to call `LinkLlmClusters` to establish a link.|

For details about `ranktable` in the preceding table, see the [Huawei Collective Communication Library (HCCL) User Guide](https://www.hiascend.com/document/redirect/CannCommunityHcclUg).

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: successful
- `LLM_PARAM_INVALID`: invalid parameter
- Other values: failed

**Troubleshooting**

None

**Constraints**

This API must be used in conjunction with `Finalize`. After successful initialization, `Finalize` must be called before any exit to ensure resource release. Otherwise, issues may arise due to unexpected resource release order.

## Finalize

**Function**

It releases LLM-DataDist resources.

**Prototype**

```
void Finalize()
```

**Parameters**

None

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

None

**Troubleshooting**

None

**Constraints**

- This API must be used in conjunction with `Initialize`.
- When the incremental cluster pulls KV data from the full cluster, ensure that the full cluster calls this API only after the incremental cluster has completed KV data synchronization. Otherwise, the operation fails.
- During KV data synchronization from the full cluster to the incremental cluster, ensure that the incremental cluster calls this API only after the full cluster has completed KV data synchronization. Otherwise, the operation fails.
- This API cannot be called concurrently with other APIs.

## SetRole

**Function**

It sets the role of LLM-DataDist.

**Prototype**

```
Status SetRole(LlmRole role, const std::map<AscendString, AscendString> &options = {})
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| role | Input| Role type, which is of the `LlmRole` type.|
| options | Input| Role parameters. For details about the supported parameters, see Table 1.|

**Table 1** Configuration items

| Configuration Item| Mandatory| Description|
| --- | --- | --- |
| OPTION_LISTEN_IP_INFO | No| - If LLM-DataDist is initialized as a client and needs to be switched to a server, set this option to the IP address and port number of the host to be listened on, for example, `192.168.1.1:26000`. Otherwise, you do not need to set this option.<br>  - If LLM-DataDist is initialized as a server and this option is not set, it indicates that LLM-DataDist is switched to a client. If it is configured, the instance stays in the server role, and any port number differing from the one used during initialization will take precedence. Example: `192.168.1.1:26001`.<br>  - When this configuration item specifies the `hixl` backend, the listening port cannot be changed using `SetRole`.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: The role is set successfully.
- `LLM_PARAM_INVALID`: invalid parameter
- `LLM_EXIST_LINK`: Residual link resources exist.
- Other values: failed

**Troubleshooting**

None

**Constraints**

Before using this API, disconnect the link from the current DataDist.

## LinkLlmClusters

**Function**

It establishes a link between two LLM-DataDist clusters. This API is called by the client.

**Prototype**

```
Status LinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| clusters | Input| Cluster information required for link establishment. The type is `ClusterInfo`. You need to configure `remote_cluster_id` and `remote_ip_infos` in `ClusterInfo`. If `OPTION_LOCAL_COMM_RES` is not specified in `Initialize(LlmDataDist)`, you also need to configure `local_ip_infos`. Only one `ip_info` can be configured for `local_ip_infos` and `remote_ip_infos` of each `ClusterInfo`.|
| rets | Output| Link establishment result of each cluster.|
| timeout_in_millis | Input| Link establishment timeout interval, in milliseconds. The default interval is `1000` ms.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: The API returns a success message only when the link establishment is successful for all clusters.
- Other values: The link fails to be established. You need to check the link establishment result of each cluster in `rets`.

**Troubleshooting**

- `LLM_ALREADY_LINK`: A link has been established between the current cluster and the remote cluster.
- `LLM_LINK_FAILED`: The link fails to be established.

**Constraints**

- Before calling this API, call the `Initialize` API on the client and server to complete initialization.
- A maximum of 512 links can be created. Creating too many links may lead to OOM errors and performance degradation during KV cache transmission. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products
- It is recommended that the timeout period be set to a value greater than `200` ms. If TLS is enabled, it is recommended that the timeout period be set to a value greater than `2000` ms. Run the following command to query the TLS status:

    ```
    hccn_tool [-i %d] -tls -g [host]
    ```

- Before calling this API, register all memory objects on the client and server. Otherwise, memory objects registered after link establishment will not support remote access.
- In container scenarios, if `OPTION_LOCAL_COMM_RES` is not configured or is left empty, map the `/etc/hccn.conf` file in the container or ensure that `hccn_tool` exists in the default path `/usr/local/Ascend/driver/tools`. If neither of the conditions is met, add the path of `hccn_tool` to the `PATH` environment variable. The following is a configuration example, where `hccn_tool_install_path` indicates the path of `hccn_tool`. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products

    ```
    export PATH=$PATH:{hccn_tool_install_path}
    ```

## UnlinkLlmClusters

**Function**

It disconnects a link between LLM-DataDist clusters.

**Prototype**

```
Status UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000, bool force_flag = false)
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| clusters | Input| Information about the clusters whose links are to be disconnected. The type is `ClusterInfo`.|
| rets | Output| Link disconnection result of each cluster.|
| timeout_in_millis | Input| Link disconnection timeout interval, in milliseconds.|
| force_flag | Input| Whether to forcibly disconnect the connection. It is disabled by default.<br>For forcible link disconnection, only the local link is forcibly disconnected; therefore, this API must be called on both ends.<br>Non-forcible link disconnection is initiated on the client. If no link fault occurs, the link is disconnected on both ends. However, if a link fault occurs, forcible link disconnection must be initiated on the server, which may take a long time.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `SUCCESS`: A success message is returned only when links are successfully disconnected for all clusters.
- Other values: Link disconnection failed. Check the link disconnection result of each cluster in `rets`.

**Troubleshooting**

`LLM_UNLINK_FAILED`: Link disconnection failed.

**Constraints**

Before calling this API, call the `Initialize` API to complete initialization.

## PullKvCache

**Function**

It pulls the cache from the remote node to the local cache.

**Prototype**

```
Status PullKvCache(const CacheIndex &src_cache_index,
                   const Cache &dst_cache,
                   uint32_t batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {})
```

**Parameters**

| Parameter| Input/Output| Value|
| --- | --- | --- |
| src_cache_index | Input| Index of the remote source cache.|
| dst_cache | Input| Local destination cache. You only need to specify the `cache_id` returned by `RegisterKvCache`.|
| batch_index | Input| Index of the local destination batch.|
| size | Input| If this parameter is set to an integer greater than 0, it indicates the size (in bytes) of the data to be pulled.<br>Or enter `-1` to pull the whole cache.<br>The default value is `-1`.|
| ext_param | Input| The difference between `second` and `first` in `src_layer_range` must be the same as that in `dst_layer_range`. The default values of `first` and `second` in both `src_layer_range` and `dst_layer_range` are `-1`, indicating all layers. The value range is [0, Maximum available layer index], and `first` must be less than or equal to `second`. The formula for calculating the maximum available layer index is as follows:<br>`(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1`<br>`tensor_num_per_layer` ranges from `1` to the total number of tensors in the cache, with a default value of `2`. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: successful
- `LLM_PARAM_INVALID`: invalid parameter
- `LLM_NOT_YET_LINK`: No link is established with the remote cluster.
- `LLM_TIMEOUT`: pull timeout
- `LLM_KV_CACHE_NOT_EXIST`: The local or remote KV cache does not exist.
- Other values: failed

**Constraints**

Before calling this API, call the `LinkLlmClusters` API first.

## PullKvBlocks

**Function**

It pulls the cache from the remote node to the local cache via a block list.

**Prototype**

```
Status PullKvBlocks(const CacheIndex &src_cache_index,
                    const Cache &dst_cache,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {})
```

**Parameters**

| Parameter| Input/Output| Value|
| --- | --- | --- |
| src_cache_index | Input| Index of the remote source cache.|
| dst_cache | Input| Local destination cache. You only need to specify the `cache_id` returned by `RegisterKvCache`.|
| src_blocks | Input| Block index list of the remote source cache.|
| dst_blocks | Input| Block index list of the local destination cache.|
| ext_param | Input| The difference between `second` and `first` in `src_layer_range` must be the same as that in `dst_layer_range`. The default values of `first` and `second` in both `src_layer_range` and `dst_layer_range` are `-1`, indicating all layers. The value range is [0, Maximum available layer index], and `first` must be less than or equal to `second`. The formula for calculating the maximum available layer index is as follows:<br>`(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1`<br>`tensor_num_per_layer` ranges from `1` to the total number of tensors in the cache, with a default value of `2`. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: successful
- `LLM_PARAM_INVALID`: invalid parameter
- `LLM_NOT_YET_LINK`: No link is established with the remote cluster.
- `LLM_TIMEOUT`: pull timeout
- `LLM_KV_CACHE_NOT_EXIST`: The remote KV cache does not exist.
- Other values: failed

**Constraints**

Before calling this API, call the `LinkLlmClusters` API first.

## PushKvCache

**Function**

It pushes the cache to the remote node.

**Prototype**

```
Status PushKvCache(const Cache &src_cache,
                   const CacheIndex &dst_cache_index,
                   uint32_t src_batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {});
```

**Parameters**

| Parameter| Input/Output| Value|
| --- | --- | --- |
| src_cache | Input| Local destination cache. You only need to specify the `cache_id` returned by `RegisterKvCache`.|
| dst_cache_index | Input| Index of the remote destination cache.|
| src_batch_index | Input| Index of the local source batch.|
| size | Input| It defaults to `-1`.|
| ext_param | Input| The difference between `second` and `first` in `src_layer_range` must be the same as that in `dst_layer_range`. The default values of `first` and `second` in both `src_layer_range` and `dst_layer_range` are `-1`, indicating all layers. The value range is [0, Maximum available layer index], and `first` must be less than or equal to `second`. The formula for calculating the maximum available layer index is as follows:<br>`(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1`<br>`tensor_num_per_layer` ranges from `1` to the total number of tensors in the cache, with a default value of `2`. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: successful
- `LLM_PARAM_INVALID`: invalid parameter
- `LLM_NOT_YET_LINK`: No link is established with the remote cluster.
- `LLM_TIMEOUT`: push timeout
- `LLM_KV_CACHE_NOT_EXIST`: The local or remote KV cache does not exist.
- Other values: failed

**Constraints**

Before calling this API, call the `LinkLlmClusters` API first.

## PushKvBlocks

**Function**

It pushes the cache to the remote node in block list mode.

**Prototype**

```
Status PushKvBlocks(const Cache &src_cache,
                    const CacheIndex &dst_cache_index,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {});
```

**Parameters**

| Parameter| Input/Output| Value|
| --- | --- | --- |
| src_cache | Input| Local destination cache. You only need to specify the `cache_id` returned by `RegisterKvCache`.|
| dst_cache_index | Input| Index of the remote destination cache.|
| src_blocks | Input| Block index list of the source cache.|
| dst_blocks | Input| Block index list of the destination cache.|
| ext_param | Input| The difference between `second` and `first` in `src_layer_range` must be the same as that in `dst_layer_range`. The default values of `first` and `second` in both `src_layer_range` and `dst_layer_range` are `-1`, indicating all layers. The value range is [0, Maximum available layer index], and `first` must be less than or equal to `second`. The formula for calculating the maximum available layer index is as follows:<br>`(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1`<br>`tensor_num_per_layer` ranges from `1` to the total number of tensors in the cache, with a default value of `2`. When `src_layer_range` or `dst_layer_range` uses non-default values, `tensor_num_per_layer` can be left as default or set to a custom value that is divisible by the total number of cached tensors.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: successful
- `LLM_PARAM_INVALID`: invalid parameter
- `LLM_NOT_YET_LINK`: No link is established with the remote cluster.
- `LLM_TIMEOUT`: push timeout
- `LLM_KV_CACHE_NOT_EXIST`: The local or remote KV cache does not exist.
- Other values: failed

**Constraints**

Before calling this API, call the `LinkLlmClusters` API first.

## RegisterKvCache

**Function**

It registers the local KV cache memory.

**Prototype**

```
Status RegisterKvCache(const CacheDesc &cache_desc,
                       const std::vector<uint64_t> &addrs,
                       const RegisterCfg &cfg,
                       int64_t &cache_id);
```

**Parameters**

| Parameter| Input/Output| Value|
| --- | --- | --- |
| cache_desc | Input| Description of the local cache.|
| addrs | Input| Addresses of the local cache. The number of addresses shall not exceed 240.|
| cfg | Input| Reserved parameter.|
| cache_id | Output| ID of the registered cache. It can be used to construct a cache reference when KV transmission APIs are called.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: successful
- Other values: failed

**Constraints**

Before calling this API, call the `Initialize` API to complete initialization.

If the HDK version is earlier than 25.5.0, a maximum of 20 GB host memory can be registered. If the HDK version is 25.5.0 or later, a maximum of 1 TB host memory can be registered. The larger the registered memory is, the more the OS memory is occupied. The following products are supported:

- Atlas A2 training products and Atlas A2 inference products
- Atlas A3 training products and Atlas A3 inference products

## UnregisterKvCache

**Function**

It deregisters the local KV cache memory.

**Prototype**

```
Status UnregisterKvCache(int64_t cache_id);
```

**Parameters**

| Parameter| Input/Output| Value|
| --- | --- | --- |
| cache_id | Input| Cache ID generated during local registration. If the specified `cache_id` does not exist, `LLM_SUCCESS` is returned by default.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `LLM_SUCCESS`: successful
- Other values: failed

**Constraints**

Before calling this API, call the `Initialize` API to complete initialization. The value of `cache_id` must be the value returned by the `RegisterKvCache` API.
