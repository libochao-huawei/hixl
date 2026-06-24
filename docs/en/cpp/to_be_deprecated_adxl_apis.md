# To Be Deprecated_ADXL APIs

## Supported Products

|Product|Supported|
|--|:-:|
|Ascend 950PR and Ascend 950DT|√|
|Atlas A3 training products and Atlas A3 inference products|√|
|Atlas A2 training products and Atlas A2 inference products|√|

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## AdxlEngine Constructor

**Function**

It creates an AdxlEngine object.

**Prototype**

```
AdxlEngine()
```

**Parameters**

None

**Returns**

None

**Troubleshooting**

None

**Constraints**

None


## \~AdxlEngine\(\)

**Function**

It destructs an AdxlEngine object.

**Prototype**

```
~AdxlEngine()
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

It initializes an AdxlEngine instance. This API must be called before other APIs are called.

**Prototype**

```
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**Parameters**

|Parameter Name|Input/Output|Description|
|--|--|--|
|local_engine|Input|AdxlEngine identifier, which must be unique among all participants in link establishment. The format is `host_ip:host_port` or `host_ip`. You are not advised to set this parameter to a loopback IP address because loopback IP addresses are prone to conflicts in scenarios where multiple AdxlEngine objects interact with each other. If `host_port` is set and its value is greater than 0, the current AdxlEngine functions as the server and needs to listen to the configured port. If `host_port` is not set or its value is less than or equal to 0, the current AdxlEngine functions as the client and does not start listening.|
|options|Input|Initialization parameter value. For details, see Table 1.|


**Table 1** options

|Parameter Name|Mandatory|Description|
|--|--|--|
|OPTION_BUFFER_POOL|No|The value is a character string `BufferPool`. In scenarios where the intermediate buffer is required for transmission: When direct H2H transmission via the HCCS protocol is not supported. When the size of RDMA-registered host memory is limited. When multiple small memory blocks (for example, 128 KB) need to be transmitted through relay transmission for performance optimization. This option can be used to configure the size of the relay memory pool. The value format is `$BUFFER_NUM:$BUFFER_SIZE`. By default, the system sets this option to `4:8` (unit: MB). You can set this option to `0:0` to disable the relay memory pool. In scenarios with concurrency, you are advised to increase the value of `$BUFFER_NUM`. In addition, the same value must be configured in all places where this option is used.|
|OPTION_RDMA_TRAFFIC_CLASS|No|The value is a string `RdmaTrafficClass`. This option specifies the traffic class of the RDMA NIC. If both this option and the environment variable `HCCL_RDMA_TC` are configured, this option takes precedence. If only one of them is configured, the configured one takes effect. The value range is [0, 255]. The value must be an integer multiple of 4. The default value is 132. For more information, see the *Environment Variable Reference*.|
|OPTION_RDMA_SERVICE_LEVEL|No|The value is a string `RdmaServiceLevel`. This option specifies the service level of the RDMA NIC. This option has the same function as the environment variable `HCCL_RDMA_SL`. If both are configured, this option takes precedence. If only one of them is configured, the configured one takes effect. The value range is [0, 7]. The default value is `4`. For more information, see the *Environment Variable Reference*.|


**Example**

Go to [Gitee](https://gitee.com/ascend/samples/tags), download the sample package of the matching version based on the tag name, and obtain the sample from the `cplusplus/level1_single_api/12_adxl` directory.

**Returns**

-   `SUCCESS`: successful
-   `PARAM_INVALID`: invalid parameter
-   Other values: failed

**Troubleshooting**

None

**Constraints**

1. This API must be used in conjunction with `Finalize`. After successful initialization, `Finalize` must be called before any exit to ensure resource release. Otherwise, issues may arise due to unexpected resource release order.
2. Before initialization, call `aclrtSetDevice`.


## Finalize

**Function**

It cleans up AdxlEngine resources.

**Prototype**

```
void Finalize()
```

**Parameters**

None

**Example**

Go to [Gitee](https://gitee.com/ascend/samples/tags), download the sample package of the matching version based on the tag name, and obtain the sample from the `cplusplus/level1_single_api/12_adxl` directory.

**Returns**

None

**Troubleshooting**

None

**Constraints**

-   This API must be used in conjunction with `Initialize`.
-   You are advised to disconnect the link and deregister the registered memory before calling `Finalize`.
-   This API can only be called by the server after all clients are disconnected. If the server exits in advance, an error will be reported during client disconnection and data transmission.
-   When the client needs to perform remote read and write operations on the server's address, the server must wait until these operations are completed before calling this API; otherwise, the operation will fail.
-   This API cannot be called concurrently with other APIs.


## RegisterMem

**Function**

It registers a memory address for use by `TransferSync` in subsequent calls. The local and remote memory addresses specified in `TransferSync` can be a subset of the registered addresses. Local memory addresses must be registered with the local AdxlEngine instance, and remote memory addresses must be registered with the remote AdxlEngine instance.

**Prototype**

```
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**Parameters**

|Parameter Name|Input/Output|Description|
|--|--|--|
|mem|Input|Description of the memory to be registered. The type is `MemDesc`.|
|type|Input|Type of the memory to be registered. The type is `MemType`.|
|mem_handle|Output|Memory handle returned after successful registration, which can be used for memory deregistration. The type is `MemHandle`.|


**Example**

Go to [Gitee](https://gitee.com/ascend/samples/tags), download the sample package of the matching version based on the tag name, and obtain the sample from the `cplusplus/level1_single_api/12_adxl` directory.

**Returns**

-   `SUCCESS`: successful
-   `PARAM_INVALID`: invalid parameter
-   Other values: failed

**Troubleshooting**

None

**Constraints**

-   Before calling `Connect` to establish a link with the peer end, all local memory must be registered.
-   A maximum of 4,000 memory objects can be registered for a single instance. Registering too many memory objects may cause device OOM risks. In addition, it takes a longer time to establish a link, causing link establishment timeout. Control the number and size of registered memory objects based on your service scenario.
-   A maximum of 50 GB device memory and 20 GB host memory can be registered. The larger the registered memory is, the more the OS memory is occupied.
-   Allocate host memory using `aclrtMallocHost` for registration. The memory address allocated by this API is automatically aligned.
-   Allocate device memory using `aclrtMalloc` for registration. If HCCS is used for transmission, the memory allocation rule must be set to `ACL_MEM_MALLOC_HUGE_ONLY`.
-   This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.


## DeregisterMem

**Function**

It deregisters memory.

**Prototype**

```
Status DeregisterMem(MemHandle mem_handle)
```

**Parameters**

|Parameter Name|Input/Output|Description|
|--|--|--|
|mem_handle|Input|Memory handle. Obtain it by calling the `RegisterMem` API.|


**Example**

Go to [Gitee](https://gitee.com/ascend/samples/tags), download the sample package of the matching version based on the tag name, and obtain the sample from the `cplusplus/level1_single_api/12_adxl` directory.

**Returns**

-   `SUCCESS`: successful
-   `PARAM_INVALID`: invalid parameter
-   Other values: failed.

**Troubleshooting**

None

**Constraints**

-   Before calling this API, call `Disconnect` to disconnect all links to ensure that all memory objects are no longer used.
-   This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.


## Connect

**Function**

It establishes a link with the remote AdxlEngine instance.

**Prototype**

```
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

|Parameter Name|Input/Output|Description|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote AdxlEngine instance. The AdxlEngine instance corresponding to `remote_engine` must be on the same server with the local instance.|
|timeout_in_millis|Input|Link establishment timeout interval, in milliseconds. The default value is `1000`.|


**Example**

Go to [Gitee](https://gitee.com/ascend/samples/tags), download the sample package of the matching version based on the tag name, and obtain the sample from the `cplusplus/level1_single_api/12_adxl` directory.

**Returns**

-   `SUCCESS`: successful
-   `PARAM_INVALID`: invalid parameter
-   `TIMEOUT`: Link establishment times out.
-   `ALREADY_CONNECTED`: link already established
-   Other values: failed

**Troubleshooting**

None

**Constraints**

-   Before calling this API, call the `Initialize` API on the client and server to complete initialization.
-   A maximum of 512 links can be created. Creating too many links may lead to OOM errors and performance degradation during KV cache transmission.
-   It is recommended that the timeout period be set to a value greater than `200` ms. If TLS is enabled, it is recommended that the timeout period be set to a value greater than `2000` ms. Run the following command to query the TLS status:

    `hccn_tool [-i %d] -tls -g [host]`

-   Before calling this API, register all local and remote memory objects. Otherwise, memory objects registered after link establishment will not support remote access.
-   In container scenarios, map the `/etc/hccn.conf` file in the container or ensure that `hccn_tool` exists in the default path `/usr/local/Ascend/driver/tools`. If neither of the conditions is met, add the path of `hccn_tool` to the `PATH` environment variable. The following is a configuration example, where `hccn_tool_install_path` indicates the path of `hccn_tool`.

    ```
    export PATH=$PATH:${hccn_tool_install_path}
    ```

-   This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.


## Disconnect

**Function**

It disconnects a link with the remote AdxlEngine instance.

**Prototype**

```
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

|**Parameter Name**|Input/Output|**Value**|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote AdxlEngine instance.|
|timeout_in_millis|Input|Link disconnection timeout interval, in milliseconds. The default value is `1000`.|


**Example**

Go to [Gitee](https://gitee.com/ascend/samples/tags), download the sample package of the matching version based on the tag name, and obtain the sample from the `cplusplus/level1_single_api/12_adxl` directory.

**Returns**

-   `SUCCESS`: successful
-   `PARAM_INVALID`: invalid parameter
-   `NOT_CONNECTED`: No connection is created with the peer end.
-   Other values: failed

**Constraints**

-   Before calling this API, call the `Initialize` API to complete initialization.
-   This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.


## TransferSync

**Function**

It transmits memory with the remote AdxlEngine instance.

**Prototype**

```
Status TransferSync(const AscendString &remote_engine,
                    TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout_in_millis = 1000)
```

**Parameters**

|**Parameter Name**|Input/Output|**Value**|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote AdxlEngine instance.|
|operation|Input|Reading the remote memory to the local side or writing the local memory to the remote side.|
|op_descs|Input|Local and remote addresses for batch operations.|
|timeout_in_millis|Input|Transmission timeout interval, in milliseconds. The default value is `1000`.|


**Example**

Go to [Gitee](https://gitee.com/ascend/samples/tags), download the sample package of the matching version based on the tag name, and obtain the sample from the `cplusplus/level1_single_api/12_adxl` directory.

**Returns**

-   `SUCCESS`: successful
-   `PARAM_INVALID`: invalid parameter
-   `NOT_CONNECTED`: No connection is created with the peer end.
-   `TIMEOUT`: transmission timeout
-   Other values: failed

**Constraints**

-   Before calling this API, call the `Connect` API to establish a link with the peer end.
-   This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.
-   By default, the relay memory pool is enabled. If the data in `op_descs` is less than 256 KB, the relay transmission mode is used by default to improve performance. Otherwise, the system determines whether to use relay transmission or direct mode based on whether there is unregistered memory.
-   When the relay memory pool is enabled, if either the local memory or the remote memory in `op_descs` is not registered, the relay transmission mode is used. In addition, the memory that has not been registered is considered as the host memory. You need to ensure that the address is valid.
-   In relay transmission mode, the transmission types of all `op_descs` must be the same. For example, all `op_descs` are written from the local host memory to the remote host memory.

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

| Parameter| Input/Output| Value|
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

| Parameter| Input/Output| Value|
| --- | --- | --- |
| req | Input| Request handle, which is generated by calling `TransferAsync`.|
| status | Output| Transfer status. The enumerated values are as follows:<br><br>- `WAITING`<br>- `COMPLETED`<br>- `TIMEOUT` (not supported currently)<br>- `FAILED`|

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
Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis = 1000)
```

**Parameters**

|**Parameter Name**|Input/Output|**Value**|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote AdxlEngine instance.|
|notify|Input|Content to be sent. The maximum length of `name` and `notify_msg` in the content is 1,024 characters.|
|timeout_in_millis|Input|Sending timeout interval, in milliseconds. The default value is `1000`.|


**Example**

None

**Returns**

-   `SUCCESS`: successful
-   Other values: failed

**Constraints**

-   Before calling this API, call the `Connect` API to establish a link with the peer end.
-   This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.
-   Each link can have a maximum of 4,096 notifications. Ensure that the remote AdxlEngine calls the `GetNotifies` API in a timely manner to consume notifications, preventing sending failures caused by reaching the upper limit.


## GetNotifies

**Function**

It obtains all notification messages received by servers in the current AdxlEngine instance and clears them.

**Prototype**

```
Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**Parameters**

|**Parameter Name**|Input/Output|**Value**|
|--|--|--|
|notifies|Output|Vector for storing notification messages.|


**Example**

None

**Returns**

-   `SUCCESS`: successful
-   Other values: failed

**Constraints**

-   Before calling this API, call the `Connect` API to establish a link with the peer end.
-   This API must run in the same thread as `Initialize`. If you need to switch to another thread to call this API, call `aclrtGetCurrentContext` in the thread where `Initialize` is called to obtain the context, and then call `aclrtSetCurrentContext` in the new thread to set the context.


## MallocMem

**Function**

It allocates memory.

**Prototype**

```
Status MallocMem(MemType type, size_t size, void **ptr);
```

**Parameters**

|**Parameter Name**|Input/Output|**Value**|
|--|--|--|
|type|Input|Memory type.|
|size|Input|Memory size.|
|ptr|Output|Pointer to the allocated memory.|


**Example**

None

**Returns**

-   `SUCCESS`: successful
-   Other values: failed

**Constraints**

Only the host memory can be allocated.


## FreeMem

**Function**

It releases the memory.

**Prototype**

```
Status FreeMem(void *ptr)
```

**Parameters**

|**Parameter Name**|Input/Output|**Value**|
|--|--|--|
|ptr|Input|Pointer to the memory to be freed.|


**Example**

None

**Returns**

-   `SUCCESS`: successful
-   Other values: failed

**Constraints**

None
