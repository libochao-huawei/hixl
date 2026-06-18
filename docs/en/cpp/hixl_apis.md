# HIXL APIs

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported. For Ascend 950PR/Ascend 950DT, `SendNotify` and `GetNotifies` are not supported.

## HIXL Constructor

**Function**

It creates an HIXL object.

**Prototype**

```
Hixl()
```

**Parameters**

None

**Returns**

None

**Troubleshooting**

None

**Constraints**

None

## \~Hixl\(\)

**Function**

It destructs an HIXL object.

**Prototype**

```
~Hixl
```

**Parameters**

None

**Returns**

None

**Troubleshooting**

None

## Initialize

**Function**

It initializes an HIXL instance. This API must be called before other APIs are called.

**Prototype**

```
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**Parameters**

| Parameter Name| Input/Output| Description|
| --- | --- | --- |
| local_engine | Input| HIXL identifier, which must be unique among all participants in link establishment. For IPv4, the format is `host_ip:host_port` or `host_ip`. For IPv6, the format is `[host_ip]:host_port` or `[host_ip]`. You are not advised to set this parameter to a loopback IP address because loopback IP addresses are prone to conflicts in scenarios where multiple HIXL objects interact with each other.<br>If `host_port` is set and its value is greater than 0, the current HIXL functions as the server and needs to listen to the configured port. If `host_port` is not set or its value is less than or equal to 0, the current HIXL functions as the client and does not start listening.|
| options | Input| Initialization parameter value. For details, see the following table.|

**Table 1** options (Atlas A2 training products/Atlas A2 inference products/Atlas A3 training products/Atlas A3 inference products)

| Parameter Name| Mandatory| Description|
| --- | --- | --- |
| OPTION_ENABLE_USE_FABRIC_MEM | No| The value is a string `EnableUseFabricMem`.<br>- `0`: The Fabric Mem mode is disabled.<br>- `1`: The Fabric Mem mode is enabled.<br><br>This option applies to scenarios where HCCS is used for D2RH and RH2D transmission.<br><br>Note: In cluster scenarios, this parameter must be set to the same value on all nodes. This parameter cannot be configured together with `OPTION_BUFFER_POOL`. Only Atlas A3 training products and Atlas A3 inference products are supported.|
| OPTION_BUFFER_POOL | No| The value is a character string `BufferPool`.<br>In scenarios where the intermediate buffer is required for transmission:<br>- When the size of RDMA-registered host memory is limited.<br>- When multiple small memory blocks (for example, 128 KB) need to be transmitted through relay transmission for performance optimization.<br>This option can be used to configure the size of the relay memory pool. The value format is `$BUFFER_NUM:$BUFFER_SIZE`. By default, the system sets this option to `4:8` (unit: MB). You can set this option to `0:0` to disable the relay memory pool. In scenarios with concurrency, you are advised to increase the value of `$BUFFER_NUM`. In addition, the same value must be configured in all places where this option is used. This parameter cannot be configured together with `OPTION_ENABLE_USE_FABRIC_MEM`.<br>Note: If this parameter is not set, the following restrictions apply:<br><br>Atlas A2 training products/Atlas A2 inference products: Only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported. When the HCCS transmission protocol is used in the server, only D2D transmission is supported.|
| OPTION_RDMA_TRAFFIC_CLASS | No| The value is a string `RdmaTrafficClass`.<br>This option specifies the traffic class of the RDMA NIC. If both this option and the environment variable `HCCL_RDMA_TC` are configured, this option takes precedence. If only one of them is configured, the configured one takes effect.<br>The value range is [0, 255]. The value must be an integer multiple of 4. The default value is 132.|
| OPTION_RDMA_SERVICE_LEVEL | No| The value is a string `RdmaServiceLevel`.<br>This option specifies the service level of the RDMA NIC. This option has the same function as the environment variable `HCCL_RDMA_SL`. If both are configured, this option takes precedence. If only one of them is configured, the configured one takes effect.<br>The value range is [0, 7]. The default value is `4`.|
| OPTION_GLOBAL_RESOURCE_CONFIG | No| The value is a string `GlobalResourceConfig`. This option is used to enable and configure global resource configuration. The configuration examples and usage restrictions of this parameter are provided below the table.|
| OPTION_AUTO_CONNECT | No| The value is a string `AutoConnect`.<br>- `0`: Auto Connect mode disabled<br>- `1`: Auto Connect mode enabled<br><br>Note:<br>- If this option is enabled, link setup can be skipped and transmission can be performed directly.<br>- If this option is enabled, abnormal links are automatically cleared when a transmission exception occurs or the peer end is destroyed. (The peer end destruction needs to be detected by the heartbeat mechanism. The default heartbeat interval is 10s.)|
| OPTION_LOCAL_COMM_RES | No| Local communication resource information, formatted as a JSON string. The configuration method is as follows:<br>Only the device information used by the current LLM-DataDist instance in `ranktable` needs to be configured; the `server_count` and `rank_id` fields are not required. For details about `ranktable`, see the [Huawei Collective Communication Library (HCCL) User Guide](https://www.hiascend.com/document/redirect/CannCommunityHcclUg). This option can be left unconfigured or set to an empty string. If left empty, the relevant information is automatically generated.|

For details about the environment variables in the preceding table, see the [Environment Variable Reference](https://www.hiascend.com/document/redirect/CannCommunityEnvRef). For details about `ranktable`, see the [Huawei Collective Communication Library (HCCL) User Guide](https://www.hiascend.com/document/redirect/CannCommunityHcclUg).
<br>The configuration examples and usage restrictions of `OPTION_GLOBAL_RESOURCE_CONFIG` are as follows:<br>For the Fabric Mem mode (supported only by Atlas A3 training products and Atlas A3 inference products), the configuration example is as follows:

```
{
    "fabric_memory.max_capacity": "128", //Size of the virtual memory pool. Value range: an integer in the range (0, 1024]. Default value: 64. Unit: TB.
    "fabric_memory.start_address": "40", //Start address of the virtual memory pool. Value range: an integer in the range [40, 220]. Default value: 40. Unit: TB.
    "fabric_memory.task_stream_num": "1", //Number of streams used by a single task in Fabric Mem mode. Value range: an integer in the range [1, 8]. Default value: 4.
}
```

<br>The following is an example of setting parameters for the link pool mechanism:

```
{
    "channel_pool.max_channel": "10", //Maximum number of links. Value range: an integer in the range (0, 512]. Default value: 512.
    "channel_pool.high_waterline": "0.3", //High watermark for triggering link destruction. Value range: a decimal number in the range (0, 1). This parameter must be configured together with channel_pool.low_waterline.
    "channel_pool.low_waterline": "0.1" //Low watermark for triggering link destruction. Value range: a decimal number in the range (0, 1). The value must be less than the high watermark.
}
```

The channel pool determines destruction based on the current channel count. If the count reaches the high watermark, it destroys (Current count – Low watermark) channels, skipping any that are actively transmitting before establishing a new connection. The relevant parameters are calculated as follows:<br>-
Channels at high watermark = max(1,static_cast<int32_t> (channel_pool.max_channel *channel_pool.high_waterline))
<br>Channels at low watermark = max(1,static_cast<int32_t> (channel_pool.max_channel* channel_pool.low_waterline))
<br>In the example above, the high and low watermarks evaluate to 3 and 1, respectively. Before each new connection, the system checks if the HIXL channel count has reached 3. If so, it destroys (Current count – 1) idle channels before proceeding with the new connection.
<br>Before enabling the link pool mechanism, pay attention to the following:
<br>- `OPTION_GLOBAL_RESOURCE_CONFIG` must be configured for all HIXL Engines in the cluster.
<br>- When the `TransferSync` or `TransferAsync` API is called, if no related link exists, a new link will be established.
<br>- This increases the extra overhead for transmission and link establishment, which may lead to performance deterioration.

**Table 2** options (Ascend 950PR/Ascend 950DT)
| Parameter Name| Mandatory| Description|
| --- | --- | --- |
| OPTION_LOCAL_COMM_RES | Yes| Local communication resource information, formatted as a JSON string. For details about the configuration format, see [Communication Resource Configuration Fields](#Communication Resource Configuration Fields). If it is left empty, related information will not be automatically generated.|
| OPTION_GLOBAL_RESOURCE_CONFIG | No| The value is a string `GlobalResourceConfig`. It is used to enable and configure global resources. The value is a JSON string. For details about the fields, [Global Resource Configuration Fields](#Global Resource Configuration Fields).|

**Communication Resource Configuration Fields** 
| Field| Data Type| Mandatory| Description| Value Option/Filling Rule|
| ---- | ---- | ---- | ---- | ---- |
| version | String| Yes| Version| `1.3`|
| net_instance_id | String| Yes| Unique ID of the current supernode.| Unique for each supernode.|
| endpoint_list | Array| Yes| List of available communication devices.| - |
| endpoint_list[].protocol | String| Yes| Communication protocol| `roce`, `ub_ctp`, `ub_tp`, `uboe`|
| endpoint_list[].comm_id | String| Yes| Communication identifier| If `protocol` is set to `ub_ctp` or `ub_tp`, set this parameter to *${eid}*. If protocol is set to `roce`, set this parameter to the IPv4 or IPv6 NIC address. If `protocol` is set to `uboe`, set this parameter to the IP address of the device uboe NIC.|
| endpoint_list[].placement | String| Yes| Communication device location| "host"/"device" |
| endpoint_list[].plane | String| No| Communication device plane| This parameter is mandatory when the device is plane-specific and the `protocol` is `ub_ctp` or `ub_tp`. The value must be unique for each plane, for example, `plane-a` or `plan-b`.|
| endpoint_list[].dst_eid | String| No| *${eid}* of the peer communication device connected to the current communication device.| If the `protocol` is `ub_ctp` and the peer device is directly connected in full-mesh mode, set this parameter to the peer *${eid}*.|

**Global Resource Configuration Fields** 
| Field| Data Type| Mandatory| Description| Value Option/Filling Rule|
| ---- | ---- | ---- | ---- | ---- |
| comm_resource_config.protocol_desc | String array| No| Communication protocol and communication device location configuration| Currently, only ["uboe:device"] is supported, indicating the use of the UBOE protocol with the communication device located on the device. If `OPTION_LOCAL_COMM_RES` is not configured, or if the `endpoint_list` within it is empty, the UBOE endpoint information will be generated automatically; otherwise, the configuration will be ignored.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `SUCCESS`: successful
- `PARAM_INVALID`: invalid parameter
- Other values: failed

**Troubleshooting**

None

**Constraints**

1. This API must be used in conjunction with `Finalize`. After successful initialization, `Finalize` must be called before any exit to ensure resource release. Otherwise, issues may arise due to unexpected resource release order.
2. Before initialization, call `aclrtSetDevice`.

## Finalize

**Function**

It cleans up HIXL resources.

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
- You are advised to disconnect the link and deregister the registered memory before calling `Finalize`.
- This API can only be called by the server after all clients are disconnected. If the server exits in advance, an error will be reported during client disconnection and data transmission.
- When the client needs to perform remote read and write operations on the server's address, the server must wait until these operations are completed before calling this API; otherwise, the operation will fail.
- This API cannot be called concurrently with other APIs.

## RegisterMem

**Function**

It registers a memory address for use by `TransferSync` in subsequent calls. The local and remote memory addresses specified in `TransferSync` can be a subset of the registered addresses. Local memory addresses must be registered with the local HIXL instance, and remote memory addresses must be registered with the remote HIXL instance.

**Prototype**

```
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**Parameters**

| Parameter Name| Input/Output| Description|
| --- | --- | --- |
| mem | Input| Description of the memory to be registered. The type is `MemDesc`.|
| type | Input| Type of the memory to be registered. The type is `MemType`.|
| mem_handle | Output| Memory handle returned after successful registration, which can be used for memory deregistration. The type is `MemHandle`.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `SUCCESS`: successful
- `PARAM_INVALID`: invalid parameter
- Other values: failed

**Troubleshooting**

None

**Constraints**

- Before calling `Connect` to establish a link with the peer end, all local memory must be registered.
- A maximum of 4,000 memory objects can be registered for a single instance. Registering too many memory objects may cause device OOM risks. In addition, it takes a longer time to establish a link, causing link establishment timeout. Control the number and size of registered memory objects based on your service scenario.
- If the HDK version is earlier than 25.5.0, a maximum of 20 GB host memory can be registered. If the HDK version is 25.5.0 or later, a maximum of 1 TB host memory can be registered. The larger the registered memory is, the more the OS memory is occupied. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products
- Allocate host memory using `aclrtMallocHost` for registration. The memory address allocated by this API is automatically aligned. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products
- Allocate device memory using `aclrtMalloc` for registration. If HCCS is used for transmission, the memory allocation rule must be set to `ACL_MEM_MALLOC_HUGE_ONLY`.
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.
- In the Ascend 950PR/Ascend 950DT scenario, if you use the host RoCE NIC, the memory allocated by `aclrtMallocHost` cannot be registered. Use the `malloc` method instead.

## DeregisterMem

**Function**

It deregisters memory.

**Prototype**

```
Status DeregisterMem(MemHandle mem_handle)
```

**Parameters**

| Parameter Name| Input/Output| Description|
| --- | --- | --- |
| mem_handle | Input| Memory handle. Obtain it by calling the `RegisterMem` API.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `SUCCESS`: successful
- `PARAM_INVALID`: invalid parameter
- Other values: failed

**Troubleshooting**

None

**Constraints**

- Before calling this API, call `Disconnect` to disconnect all links to ensure that all memory objects are no longer used.
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.

## Connect

**Function**

It establishes a link with the remote HIXL instance.

**Prototype**

```
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter Name| Input/Output| Description|
| --- | --- | --- |
| remote_engine | Input| Unique identifier of the remote HIXL instance. The HIXL instance corresponding to `remote_engine` must be on the same server with the local instance.|
| timeout_in_millis | Input| Link establishment timeout interval, in milliseconds. The default value is `1000`.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `SUCCESS`: successful
- `PARAM_INVALID`: invalid parameter
- `TIMEOUT`: Link establishment times out.
- `ALREADY_CONNECTED`: link already established
- Other values: failed

**Troubleshooting**

None

**Constraints**

- Before calling this API, call the `Initialize` API on the client and server to complete initialization.
- A maximum of 512 links can be created. Creating too many links may lead to OOM errors and performance degradation during KV cache transmission. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products
- It is recommended that the timeout period be set to a value greater than 200 ms.
- Before calling this API, register all local and remote memories. Otherwise, memories registered after link establishment will not support remote access.
- In container scenarios, map the `/etc/hccn.conf` file in the container or ensure that `hccn_tool` exists in the default path `/usr/local/Ascend/driver/tools`. If neither of the conditions is met, add the path of `hccn_tool` to the `PATH` environment variable. The following is a configuration example, where `hccn_tool_install_path` indicates the path of `hccn_tool`. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products

    ```
    export PATH=$PATH:{hccn_tool_install_path}
    ```
  
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.

## Disconnect

**Function**

It disconnects a link with the remote HIXL instance.

**Prototype**

```
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter Name| Input/Output| Value|
| --- | --- | --- |
| remote_engine | Input| Unique identifier of the remote HIXL instance.|
| timeout_in_millis | Input| Link disconnection timeout interval, in milliseconds. The default value is `1000`.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `SUCCESS`: successful
- `PARAM_INVALID`: invalid parameter
- `NOT_CONNECTED`: No connection is created with the peer end.
- Other values: failed

**Constraints**

- Before calling this API, call the `Initialize` API to complete initialization.
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.

## TransferSync

**Function**

It transmits memory with the remote HIXL instance.

**Prototype**

```
Status TransferSync(const AscendString &remote_engine,
                    TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter Name| Input/Output| Value|
| --- | --- | --- |
| remote_engine | Input| Unique identifier of the remote HIXL instance.|
| operation | Input| Reading the remote memory to the local side or writing the local memory to the remote side.|
| op_descs | Input| Local and remote addresses for batch operations.|
| timeout_in_millis | Input| Transmission timeout interval, in milliseconds. The default value is `1000`.|

**Example**

For details, see [Sample Running](../../../examples/cpp/README_en.md).

**Returns**

- `SUCCESS`: successful
- `PARAM_INVALID`: invalid parameter
- `NOT_CONNECTED`: No connection is created with the peer end.
- `TIMEOUT`: transmission timeout
- `RESOURCE_EXHAUSTED`: Resources are used up.
- Other values: failed

**Constraints**

- Before calling this API, call the `Connect` API to establish a link with the peer end. Alternatively, enable the link pool mechanism during HIXL initialization. Specifically, configure the `OPTION_GLOBAL_RESOURCE_CONFIG` parameter in `options`. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.
- By default, the relay memory pool is enabled. When the relay memory pool is enabled, if either the local memory or the remote memory in `op_desc` is not registered, the relay transmission mode is used. In addition, the memory that has not been registered is considered as the host memory. You need to ensure that the address is valid. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products
- In relay transmission mode, the transmission types of all `op_desc` must be the same. For example, all `op_desc` are written from the local host memory to the remote host memory. The following products are supported:
<br>- Atlas A2 training products and Atlas A2 inference products
<br>- Atlas A3 training products and Atlas A3 inference products
- In Fabric Mem transfer mode, the transfer types of all `op_descs` must be the same. The system determines the transfer direction based on the memory type of the first `op_desc`. The following products are supported:
<br>- Atlas A3 training products and Atlas A3 inference products

## TransferAsync

**Function**

It transmits memory in batches with the remote HIXL instance asynchronously.

**Prototype**

```
  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req)
```

**Parameters**

| Parameter Name| Input/Output| Value|
| --- | --- | --- |
| remote_engine | Input| Unique identifier of the remote HIXL instance.|
| operation | Input| Reading the remote memory to the local side or writing the local memory to the remote side.|
| op_descs | Input| Local and remote addresses for batch operations.|
| optional_args | Input| Optional parameter (reserved).|
| req | Output| Request handle, which is used to query the request status of the transfer.|

**Example**

```
  //Initialize the engines on the client and server and establish a link.
  client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
```

**Returns**

- `SUCCESS`: successful
- `NOT_CONNECTED`: No connection is created with the peer end.
- `RESOURCE_EXHAUSTED`: Resources are used up.
- Other values: failed

**Constraints**

- Before calling this API, note the following constraints:
<br>Call the `Connect` API to establish a link with the peer end.
<br>Alternatively, enable the link pool mechanism during HIXL initialization. Specifically, configure the `OPTION_GLOBAL_RESOURCE_CONFIG` parameter in `options`. The following products are supported:
  <br>- Atlas A2 training products and Atlas A2 inference products
  <br>- Atlas A3 training products and Atlas A3 inference products
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.
- Currently, asynchronous transfer supports and defaults to direct transfer. Relay transmission is not supported.
- In Fabric Mem transfer mode, the transfer types of all `op_descs` must be the same. The system determines the transfer direction based on the memory type of the first `op_desc`. The following products are supported:
<br>- Atlas A3 training products and Atlas A3 inference products

## GetTransferStatus

**Function**

It obtains the asynchronous memory transmission status.

**Prototype**

```
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status)
```

**Parameters**

| Parameter Name| Input/Output| Value|
| --- | --- | --- |
| req | Input| Request handle, which is generated by calling `TransferAsync`.|
| status | Output| Transfer status. The enumerated values are as follows:<br><br>- `WAITING` <br>- `COMPLETED`<br>- `TIMEOUT` (not supported currently)<br>- `FAILED`|

**Example**

```
  //Initialize the engines on the client and server and establish a link.
  Status transfer_status = client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  //req is the output of TransferAsync(). Use this request handle to query the transmission status.
  Status query_status = GetTransferStatus(req, status);
  //Check the transmission status to determine whether the transmission is complete.
  ...
```

**Returns**

- `SUCCESS`: successful
- `PARAM_INVALID`: invalid parameter
- `NOT_CONNECTED`: No connection is created with the peer end.
- Other values: failed

**Constraints**

- Before calling this API, call the `Connect` API to establish a link with the peer end.
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.
- Use this API to query the request status after calling the `TransferAsync` API for asynchronous transfer. If the status is `COMPLETED` or `FAILED`, related resources will be released. Repeated queries are not supported in this scenario.
- During asynchronous transfer, you need to monitor whether the task times out. If the task times out, call the `Disconnect` API to destroy the link and clear related resources.
- If an asynchronous transfer task fails, the status queried by calling this API and the status returned by the API are both `FAILED`.

## SendNotify

**Function**

It sends a notification message to the remote engine.

**Prototype**

```
  Status SendNotify(const AscendString &remote_engine,
                    const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter Name| Input/Output| Value|
| --- | --- | --- |
| remote_engine | Input| Unique identifier of the remote HIXL instance.|
| timeout_in_millis | Input| Sending timeout interval, in milliseconds.
| notify | Input| Content to be sent. The maximum length of `notify_msg` and `name` in the content is 1,024 characters.|

**Example**

None

**Returns**

- `SUCCESS`: successful
- Other values: failed

**Constraints**

- Before calling this API, call the `Connect` API to establish a link with the peer end.
- This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.
- Ascend 950PR and Ascend 950DT are not supported.
- Each link can have a maximum of 4,096 notifications. Ensure that the remote HIXL calls the `GetNotifies` API in a timely manner to consume notifications, preventing sending failures caused by reaching the upper limit.

## GetNotifies

**Function**

It obtains all notification messages received by servers in the current HIXL instance and clears them.

**Prototype**

```
  Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**Parameters**

| Parameter Name| Input/Output| Value|
| --- | --- | --- |
| notifies | Input| Vector for storing notification messages.|

**Example**

None

**Returns**

- `SUCCESS`: successful
- Other values: failed

**Constraints**

Ascend 950PR and Ascend 950DT are not supported.
