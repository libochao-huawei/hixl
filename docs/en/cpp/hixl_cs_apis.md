
# HIXL_CS APIs

## Supported Products

| Product| Supported|
|---|--- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| x |
| Atlas A2 training products and Atlas A2 inference products| x |

## HixlStatus and Return Codes

The API return code types and constants are as follows.

```
typedef uint32_t HixlStatus;
static const uint32_t HIXL_SUCCESS = 0U;
static const uint32_t HIXL_PARAM_INVALID = 103900U;
static const uint32_t HIXL_TIMEOUT = 103901U;
static const uint32_t HIXL_FAILED = 503900U;
```

The meanings of error codes are as follows.

| Enumerated Value| Meaning| Recoverable| Solution|
| --- | --- | --- | --- |
| HIXL_SUCCESS | Successful| N/A| N/A|
| HIXL_PARAM_INVALID | Incorrect parameters| Yes| Locate the fault based on logs.|
| HIXL_TIMEOUT | Process timed out| No| Preserve the environment, collect Host/Device logs, and back them up.|
| HIXL_FAILED | Common failure| No| Preserve the environment, collect Host/Device logs, and back them up.|

## Handle Types

The handle type definitions are as follows.

```
typedef void *HixlServerHandle;
typedef void *HixlClientHandle;
typedef void *CompleteHandle;
typedef void *MemHandle;
```

## Struct Description

The following describes the struct definitions used in CS APIs.

### HixlServerConfig

Reserved field for the server configuration, which is used for future extension.

```
struct HixlServerConfig {
  uint8_t reserved[128] = {};
};
```

### HixlClientConfig

Reserved field for the client configuration, which is used for future extension.

```
struct HixlClientConfig {
  uint8_t reserved[128] = {};
};
```

### HixlClientDesc

Client description.

```
struct HixlClientDesc {
  const EndpointDesc *local_endpoint;
  const EndpointDesc *remote_endpoint;
  const char *server_ip;
  uint32_t server_port;
  uint8_t tc;
  uint8_t sl;
  uint8_t reserved[98] = {};
};
```

Field Description

| Field| Type| Description|
|---|---|---|
| local_endpoint | const EndpointDesc* | Local endpoint description. For details, see the [definition](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h).|
| remote_endpoint | const EndpointDesc* | Remote endpoint description. For details, see the [definition](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h).|
| server_ip | const char* | Host IP address of the target server.|
| server_port | uint32_t | Host port of the target server.|
| tc | uint8_t | Traffic class of the RDMA NIC.|
| sl | uint8_t | Service level of the RDMA NIC.|
| reserved | uint8_t[98] | Reserved field for the `HixlClientDesc` configuration, which is used for future extension. The total size of the structure is 128 bytes.|

### HixlServerDesc

Server description.

```
struct HixlServerDesc {
  const EndpointDesc *endpoint_list;
  const char *server_ip;
  uint32_t server_port;
  uint32_t endpoint_list_num;
  uint8_t reserved[104] = {};
};
```

Field Description

| Field| Type| Description|
|---|---|---|
| server_ip | const char* | Listening IP address.|
| server_port | uint32_t | Listening port.|
| endpoint_list | const EndpointDesc* | Pointer to the endpoint description array. For details, see the [definition](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h).|
| endpoint_list_num | uint32_t | Number of endpoints.|
| reserved | uint8_t[104] | Reserved field for `HixlServerDesc` configuration, which is used for future extension. The total size of the structure is 128 bytes.|

### HixlOneSideOpDesc

Unidirectional operation description (used for batch put/get).

```
struct HixlOneSideOpDesc {
  void *remote_buf;
  void *local_buf;
  uint64_t len;
};
```

Field Description

| Field| Type| Description|
|---|---|---|
| remote_buf | void* | Remote (server) data address.|
| local_buf | void* | Local (client) data address.|
| len | uint64_t | Transfer length (in bytes).|

Semantic meanings of Put and Get for `HixlOneSideOpDesc`:

- `BatchPut` (synchronous/asynchronous): Data is copied from `local_buf` to `remote_buf`. The length is `len`.
- `BatchGet` (synchronous/asynchronous): Data is copied from `remote_buf`  to `local_buf`. The length is `len`.

## Enumeration

Definition of the completion status of an asynchronous task.

```
enum HixlCompleteStatus {
  HIXL_COMPLETE_STATUS_WAITING,
  HIXL_COMPLETE_STATUS_COMPLETED,
  HIXL_COMPLETE_STATUS_TIMEOUT,
  HIXL_COMPLETE_STATUS_FAILED
};

Note:
`HIXL_COMPLETE_STATUS_TIMEOUT` and `HIXL_COMPLETE_STATUS_FAILED` are reserved fields.
```

## API Description

Each API includes the function description, function prototype, parameter description, return value, example, and restrictions.

### HixlCSServerCreate

**Function**

It creates and initializes a server instance.

**Prototype**

```
HixlStatus HixlCSServerCreate(const HixlServerDesc *server_desc,
                              const HixlServerConfig *config,
                              HixlServerHandle *server_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| server_desc | Input| Server description, including the listening IP address/port and endpoint list.|
| config | Input| Server configuration, which is reserved and not used currently.|
| server_handle | Output| It returns the created `HixlServerHandle`.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failure or error (check the constants to get the error code)

**Constraints**

- If the endpoint of the local device is used, call `aclrtSetDevice` before calling this API.
- If the API call fails, `server_handle` may be invalid.

### HixlCSServerRegMem

**Function**

It registers the shared memory for the client to access.

**Prototype**

```
HixlStatus HixlCSServerRegMem(HixlServerHandle server_handle,
                              const char *mem_tag,
                              const CommMem *mem,
                              MemHandle *mem_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| server_handle | Input| Server instance handle.|
| mem_tag | Input| (Optional) Memory tag string, which is used to identify the memory. It can be set to `NULL`.|
| mem | Input| It points to `CommMem` and describes the memory location and size. For details, see the [definition](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h).|
| mem_handle | Output| It returns the memory handle, which is used for subsequent deregistration.|

**Returns**

- `HIXL_SUCCESS`: The registration is successful.
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

- Before the client calls `HixlCSClientConnect` to establish a link with the local end, all local memory must be registered.
- If you use the host RoCE NIC, the memory allocated by `aclrtMallocHost` cannot be registered. Use the `malloc` method instead.
- Allocate device memory using `aclrtMalloc` for registration.

### HixlCSServerListen

**Function**

It starts server listening to accept connections from the client.

**Prototype**

```
HixlStatus HixlCSServerListen(HixlServerHandle server_handle, uint32_t backlog);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| server_handle | Input| Server instance handle.|
| backlog | Input| Maximum length of the connection queue.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

Before the client calls `HixlCSClientConnect` to establish a link with the local end, listening must be completed.

### HixlCSServerUnregMem

**Function**

It deregisters the memory that has been registered.

**Prototype**

```
HixlStatus HixlCSServerUnregMem(HixlServerHandle server_handle, MemHandle mem_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| server_handle | Input| Server instance handle.|
| mem_handle | Input| Memory handle to be deregistered (returned by `HixlCSServerRegMem`).|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

Before deregistration, ensure that the memory is no longer accessed by the remote end and the current server is disconnected from all clients.

### HixlCSServerDestroy

**Function**

It destroys a server instance and releases related resources.

**Prototype**

```
HixlStatus HixlCSServerDestroy(HixlServerHandle server_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| server_handle | Input| Server instance handle.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

- This API must be used in conjunction with `HixlCSServerCreate`.
- You are advised to disconnect the link and deregister the registered memory before calling `HixlCSServerDestroy`.
- This API can only be called by the server after all clients are disconnected. If the server exits in advance, an error will be reported during client disconnection and data transmission.
- When the client needs to perform remote read and write operations on the server's address, the server must wait until these operations are completed before calling this API; otherwise, the operation will fail.
- This API cannot be called concurrently with other APIs.

### HixlCSClientCreate

**Function**

It creates a client instance and initializes local resources.

**Prototype**

```
HixlStatus HixlCSClientCreate(const HixlClientDesc *client_desc,
                              const HixlClientConfig *config,
                              HixlClientHandle *client_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_desc | Input| Client description, including the server IP address/port and endpoint.|
| config | Input| Client configuration, which is reserved and not used currently.|
| client_handle | Output| Created client handle.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

- If the endpoint of the local device is used, call `aclrtSetDevice` before calling this API.
- If the API call fails, `client_handle` may be invalid.

### HixlCSClientConnect

**Function**

It initiates a synchronous connection setup with the server (blocking call until success or timeout).

**Prototype**

```
HixlStatus HixlCSClientConnect(HixlClientHandle client_handle, uint32_t timeout_ms);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle index.|
| timeout_ms | Input| Connection timeout interval, in milliseconds.|

**Returns**

- `HIXL_SUCCESS`: The connection is successful.
- `HIXL_PARAM_INVALID`: incorrect parameter
- `HIXL_TIMEOUT`: The connection times out.
- Other values: failed
  
**Constraints**

- Before calling this API to establish a link with the server, ensure that all local memory has been registered.
- Before calling this API to establish a link with the server, ensure that the server is in the listening state.
- After calling this API to establish a link, call the `HixlCSClientGetRemoteMem` API to ensure that the remote memory description is exchanged to the local end.

### HixlCSClientGetRemoteMem

**Function**

It obtains the information about the memory registered by the server.

**Prototype**

```
HixlStatus HixlCSClientGetRemoteMem(HixlClientHandle client_handle,
                                    CommMem **remote_mem_list,
                                    char ***mem_tag_list,
                                    uint32_t *list_num,
                                    uint32_t timeout_ms);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle index.|
| remote_mem_list | Output| Pointer to the returned `CommMem` array. The memory lifecycle is managed by the API. If this API is called repeatedly, the memory allocated last time is released. Copy it to the local memory if necessary. For details, see the [definition](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h).|
| mem_tag_list | Output| Character string array, corresponding to the label of each `CommMem`. The memory lifecycle is managed by the API. If this API is called repeatedly, the memory allocated last time is released.|
| list_num | Output| Length of the returned list. The memory lifecycle is managed by the API. If this API is called repeatedly, the memory allocated last time is released.|
| timeout_ms | Input| Request timeout interval, in milliseconds.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failure or timeout

**Constraints**

- The memory lifecycle of the output parameter is managed by the API. If this API is called repeatedly, the memory allocated last time is released.
- After calling the `HixlCSClientConnect` API, call this API to ensure that the remote memory description is exchanged to the local end.

### HixlCSClientRegMem

**Function**

It registers the local memory for the client.

**Prototype**

```
HixlStatus HixlCSClientRegMem(HixlClientHandle client_handle,
                              const char *mem_tag,
                              const CommMem *mem,
                              MemHandle *mem_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle index.|
| mem_tag | Input| Memory label string.|
| mem | Input| `CommMem` description.|
| mem_handle | Output| Returned memory handle.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

- Before calling `HixlCSClientConnect` to establish a link with the server, all local memory must be registered.
- If you use the host RoCE NIC, the memory allocated by `aclrtMallocHost` cannot be registered. Use the `malloc` method instead.
- Allocate device memory using `aclrtMalloc` for registration.

### HixlCSClientUnregMem

**Function**

It deregisters the memory registered by the client.

**Prototype**

```
HixlStatus HixlCSClientUnregMem(HixlClientHandle client_handle, MemHandle mem_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle index.|
| mem_handle | Input| Memory handle to be deregistered.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

Before deregistration, ensure that the memory is no longer accessed by the local end and the local end is disconnected from the server.

### HixlCSClientBatchPutAsync

**Function**

It writes multiple groups of data to the server asynchronously in batches.

**Prototype**

```
HixlStatus HixlCSClientBatchPutAsync(HixlClientHandle client_handle,
                                     uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list,
                                     CompleteHandle *complete_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle index.|
| list_num | Input| Number of subtasks, which must be greater than `0`. If the value is `0`, `HIXL_PARAM_INVALID` is returned.|
| desc_list | Input| Pointer to the `HixlOneSideOpDesc` array. The length is `list_num`. This parameter cannot be `NULL` when `list_num` is greater than `0`. Each item is in the format of `local_buf → remote_buf`, and `len` indicates the number of bytes.|
| complete_handle | Output| Returned completion handle, which is used to asynchronously query the task status. After the query is successful, related resources are automatically released. It cannot be `NULL`.|

**Returns**

- `HIXL_SUCCESS`: The task is submitted successfully (not completed).
- `HIXL_PARAM_INVALID`: incorrect parameter
- Others: Submission failed.

**Constraints**

A maximum of 4,000 data records can be concurrently transferred. After a task is delivered, the `HixlCSClientQueryCompleteStatus` API must be called in a timely manner to query the task status.

### HixlCSClientBatchGetAsync

**Function**

It reads multiple groups of data from the server to the local host asynchronously in batches.

**Prototype**

```
HixlStatus HixlCSClientBatchGetAsync(HixlClientHandle client_handle,
                                     uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list,
                                     CompleteHandle *complete_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle index.|
| list_num | Input| Number of subtasks, which must be greater than `0`. If the value is `0`, <idp:inline displayname="code" id="code1499175014475">HIXL_PARAM_INVALID</idp:inline> is returned.|
| desc_list | Input| Pointer to the <idp:inline displayname="code" id="code1691565254714">HixlOneSideOpDesc</idp:inline> array. The length is <idp:inline displayname="code" id="code129154523473">list_num</idp:inline>. This parameter cannot be `NULL` when `list_num` is greater than `0`. Each item is in the format of `remote_buf → local_buf`, and `len` indicates the number of bytes.|
| complete_handle | Output| Returned completion handle, which is used to asynchronously query the task status. After the query is successful, related resources are automatically released. It cannot be `NULL`.|

**Returns**

- `HIXL_SUCCESS`: The task is submitted successfully (not completed).
- `HIXL_PARAM_INVALID`: incorrect parameter
- Others: Submission failed.

**Constraints**

A maximum of 4,000 data records can be concurrently transferred. After a task is delivered, the `HixlCSClientQueryCompleteStatus` API must be called in a timely manner to query the task status.

### HixlCSClientBatchPutSync

**Function**

It synchronously writes multiple groups of data to the server in batches. Before the entire batch transmission is complete, times out, or fails, the current thread is blocked and `CompleteHandle` is not returned.

**Prototype**

```
HixlStatus HixlCSClientBatchPutSync(HixlClientHandle client_handle,
                                    uint32_t list_num,
                                    const HixlOneSideOpDesc *desc_list,
                                    uint32_t timeout_ms);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle, which cannot be `NULL`.|
| list_num | Input| Number of subtasks, which must be greater than `0`. If the value is `0`, <idp:inline displayname="code" id="code1099150184714">HIXL_PARAM_INVALID</idp:inline> is returned.|
| desc_list | Input| `HixlOneSideOpDesc` array whose length is `list_num`. This parameter cannot be `NULL` when `list_num` is greater than `0`. The data direction is from `local_buf` (source) to `remote_buf` (destination). `len` indicates the number of bytes transferred each time.|
| timeout_ms | Input| Total waiting timeout of the entire batch of tasks, in milliseconds. If the operation times out, `HIXL_TIMEOUT` is returned. The host implements polling to wait for completion, while the device uses stream synchronization timeout budgeting.|

**Returns**

- `HIXL_SUCCESS`: The entire batch transfer is complete.
- `HIXL_PARAM_INVALID`: Invalid parameter (for example, `list_num` == `0`, `desc_list` is invalid, or `client_handle` is `NULL`).
- `HIXL_TIMEOUT`: not completed within `timeout_ms`
- Other: transmission or internal error (see the log and `HixlStatus` code)

**Constraints**

- Before calling this function, ensure that a connection to the server has been established (for example, by calling `HixlCSClientConnect`), and the addresses in `desc_list` must be valid unilateral access addresses that have been registered or obtained through `HixlCSClientGetRemoteMem`.
- Similar to asynchronous APIs, pay attention to the restrictions on the maximum number of concurrent subtasks (see the description of asynchronous batch APIs).

### HixlCSClientBatchGetSync

**Function**

It synchronously reads multiple groups of data from the server to the local host in batches. Before the entire batch transmission is complete, times out, or fails, the current thread is blocked and `CompleteHandle` is not returned.

**Prototype**

```
HixlStatus HixlCSClientBatchGetSync(HixlClientHandle client_handle,
                                    uint32_t list_num,
                                    const HixlOneSideOpDesc *desc_list,
                                    uint32_t timeout_ms);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle, which cannot be `NULL`.|
| list_num | Input| Number of subtasks, which must be greater than `0`. If the value is `0`, <idp:inline displayname="code" id="code3100450174718">HIXL_PARAM_INVALID</idp:inline> is returned.|
| desc_list | Input| `HixlOneSideOpDesc` array whose length is `list_num`. This parameter cannot be `NULL` when `list_num` is greater than `0`. The data direction is from `remote_buf` (source) to `local_buf` (destination). `len` indicates the number of bytes transferred each time.|
| timeout_ms | Input| Total waiting timeout of the entire batch of tasks, in milliseconds. If the operation times out, `HIXL_TIMEOUT` is returned. The host implements polling to wait for completion, while the device uses stream synchronization timeout budgeting.|

**Returns**

- `HIXL_SUCCESS`: The entire batch transfer is complete.
- `HIXL_PARAM_INVALID`: Invalid parameter (for example, `list_num` == `0`, `desc_list` is invalid, or `client_handle` is `NULL`).
- `HIXL_TIMEOUT`: not completed within `timeout_ms`
- Other: transmission or internal error (see the log and `HixlStatus` code)

**Constraints**

- Before calling this function, ensure that a connection to the server has been established (for example, by calling `HixlCSClientConnect`), and the addresses in `desc_list` must be valid unilateral access addresses that have been registered or obtained through `HixlCSClientGetRemoteMem`.
- Similar to asynchronous APIs, pay attention to the restrictions on the maximum number of concurrent subtasks (see the description of asynchronous batch APIs).

### HixlCSClientQueryCompleteStatus

**Function**

It queries the completion status of an asynchronous batch task.

**Prototype**

```
HixlStatus HixlCSClientQueryCompleteStatus(HixlClientHandle client_handle,
                                           CompleteHandle complete_handle,
                                           HixlCompleteStatus *complete_status);
```

**Parameters**

| Parameter| Input/Output| Description|
| --- | --- | --- |
| client_handle | Input| Client handle index.|
| complete_handle | Input| Handle of the task to be queried.|
| complete_status | Output| It returns the task status enumeration (see `HixlCompleteStatus`).|

**Returns**

- `HIXL_SUCCESS`: The query is successful. The transfer status needs to be determined based on `complete_status`.
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

- A maximum of 4,000 data records can be concurrently transferred. After a task is delivered, the `HixlCSClientQueryCompleteStatus` API must be called in a timely manner to query the task status.
- After the transmission task status is queried and found to be `HIXL_COMPLETE_STATUS_COMPLETED`, related resources are automatically released. The same `complete_handle` cannot be used to perform another query.
- If the transmission task status is `HIXL_COMPLETE_STATUS_WAITING`, you need to determine whether the current transmission task has timed out. If the task has timed out, you can re-establish the transmission link and destroy the current abnormal link.

### HixlCSClientDestroy

**Function**

It destroys a client instance and releases resources.

**Prototype**

```
HixlStatus HixlCSClientDestroy(HixlClientHandle client_handle);
```

**Parameters**

| Parameter| Input/Output| Description|
|---|---:|---|
| client_handle | Input| Client handle index.|

**Returns**

- `HIXL_SUCCESS`: successful
- `HIXL_PARAM_INVALID`: incorrect parameter
- Other values: failed

**Constraints**

- This API must be used in conjunction with `HixlCSClientCreate`.
- You are advised to disconnect the link and deregister the registered memory before calling `HixlCSClientDestroy`.
- This API cannot be called concurrently with other APIs.

## Usage Suggestions

1. Set up the environment: Call `aclrtSetDevice(device_id)`.
2. Prepare the endpoint description (`EndpointDesc`).
3. Process on the server:

- Construct `HixlServerDesc` and `HixlServerConfig`.
- Call `HixlCSServerCreate` to create a `HixlServerHandle`.
- Allocate and prepare the `CommMem` (host/device memory) to be accessed by the remote end. Call `HixlCSServerRegMem` to register the memory and save the returned `MemHandle`.
- Call `HixlCSServerListen` to start listening for connections.
- Wait for the client to establish a connection and initiate data transmission.
- After the transmission is complete, call `HixlCSServerUnregMem` to deregister the memory and `HixlCSServerDestroy` to destroy the service.

4. Process on the client:

- Construct `HixlClientDesc` and `HixlClientConfig`.
- Call `HixlCSClientCreate` to create `HixlClientHandle`.
- Prepare the local `CommMem` and register the memory using `HixlCSClientRegMem` (save the `MemHandle`).
- Call `HixlCSClientConnect` to establish a connection (blocking or waiting until timeout) and ensure that the server is in the listening state.
- Call `HixlCSClientGetRemoteMem` to obtain the memory registered by the server and obtain the remote address for subsequent operations.
- Construct a group of `HixlOneSideOpDesc` (local/remote address and length). Select either of the following:
  - Call `HixlCSClientBatchPutAsync` or `HixlCSClientBatchGetAsync` to submit asynchronous batch operations, and poll `HixlCSClientQueryCompleteStatus` until the status is `HIXL_COMPLETE_STATUS_COMPLETED`.
  - Alternatively, call `HixlCSClientBatchPutSync` or `HixlCSClientBatchGetSync` to complete the entire batch transfer synchronously (the entire batch timeout period `timeout_ms` needs to be passed).
- Read or verify the memory content as required, and then call `HixlCSClientUnregMem` to deregister the local memory and `HixlCSClientDestroy` to destroy the client.

## Example

The following provides simplified code snippets of the process, highlighting the key steps and sequence (excluding detailed error handling and platform initialization code).

```c
// --- Server (pseudo-code) ---
HixlServerHandle server = NULL;
HixlServerDesc sdesc = {"0.0.0.0", 12345, &endpoint, 1};
HixlCSServerCreate(&sdesc, NULL, &server);

// Allocate and initialize the server memory (host or device).
CommMem server_mem = { .addr = server_buf, .size = size, .type = COMM_MEM_TYPE_DEVICE };
MemHandle server_mem_h = NULL;
HixlCSServerRegMem(server, "server_mem", &server_mem, &server_mem_h);

HixlCSServerListen(server, 1024);

// Wait until the client initiates and completes the transmission (synchronization can be performed using TCP signaling).

// Deregister and destroy the memory.
HixlCSServerUnregMem(server, server_mem_h);
HixlCSServerDestroy(server);

// --- Client (pseudo-code) ---
HixlClientHandle client = NULL;
HixlClientDesc cdesc = {"server.ip", 12345, &local_ep, &remote_ep};
HixlCSClientCreate(&cdesc, NULL, &client);

// Allocate and register the local memory.
CommMem client_mem = { .addr = client_buf, .size = size, .type = COMM_MEM_TYPE_DEVICE };
MemHandle client_mem_h = NULL;
HixlCSClientRegMem(client, "client_mem", &client_mem, &client_mem_h);

// Establish a link.
HixlCSClientConnect(client, 5000);

// Obtain the remote memory address (using mem_tag).
CommMem *remote_list = NULL; char **tags = NULL; uint32_t num = 0;
HixlCSClientGetRemoteMem(client, &remote_list, &tags, &num, 2000);

// Construct the batch operation description. Example: varying block size.
std::vector<HixlOneSideOpDesc> ops = ...; // Fill in the local/remote address and length.

// Method 1: asynchronous + polling
CompleteHandle ch;
HixlCSClientBatchPutAsync(client, ops.size(), ops.data(), &ch);
HixlCompleteStatus st;
do {
  HixlCSClientQueryCompleteStatus(client, ch, &st);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
} while (st == HIXL_COMPLETE_STATUS_WAITING);

// Method 2: synchronous (If the entire batch is completed within the timeout period, HIXL_SUCCESS is returned.)
// HixlCSClientBatchPutSync(client, ops.size(), ops.data(), 5000);

// Clear resources.
HixlCSClientUnregMem(client, client_mem_h);
HixlCSClientDestroy(client);
```

---
