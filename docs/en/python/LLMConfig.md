# LLMConfig

## Product Support

| Product | Supported |
| --- | --- |
| Ascend 950PR/Ascend 950DT | Yes |
| Atlas A3 training series products/Atlas A3 inference series products | Yes |
| Atlas A2 training series products/Atlas A2 inference series products | Yes |

Note: For Atlas A2 training series products/Atlas A2 inference series products, only Atlas 800I A2 inference server/A200I A2 Box heterogeneous component.

## LLMConfig Constructor

**Function**

Constructs LLMConfig. The init API requires a configuration dictionary. This class simplifies construction of that configuration dictionary.

Decode and Prompt can pull Cache from each other. In this mode, related APIs include enable\_cache\_manager, mem\_pool\_cfg, and listen\_ip\_info.

**Prototype**

```
__init__()
```

**Parameters**

None

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
```

**Return Value**

Returns an LLMConfig instance.

**Constraints**

None

## Generate\_options

**Function**

Generates a configuration dictionary.

**Prototype**

```
generate_options()
```

**Parameters**

None

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
...
engine_options = llm_config.generate_options()
```

**Return Value**

Returns a configuration dictionary.

**Constraints**

None

## Device\_id

**Function**

Set current process Device ID. It corresponds tounderlying ge.exec.deviceId configuration item.

**Prototype**

```
device_id(device_id)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| device_id | Union[int, List[int], Tuple[int]] | Sets the Device ID of the current process. Currently, only one Device ID can be configured. |

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.device_id = 0
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None

## Sync\_kv\_timeout

**Function**

Configures the timeout of the pull\_cache, pull\_blocks, push\_blocks, and push\_cache APIs. It corresponds to the underlying llm.SyncKvCacheWaitTime configuration item.

**Prototype**

```
sync_kv_timeout(sync_kv_timeout)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| sync_kv_timeout | int or str | Synchronous KV timeout, in ms. If not configured, the default value is 1000 ms. |

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.sync_kv_timeout = 1000
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None

## Ge\_options

**Function**

Configures additional GEconfiguration item.

**Prototype**

```
ge_options(ge_options)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| ge_options | dict[str, str] | Configures GE options.<br>ge.flowGraphMemMaxSize is important. It indicates the maximum memory occupied by all KV cache. If it is set too large, the available memory for the model is reduced. Specify it based on the actual scenario. |

**Example**

```
from llm_datadist import LLMConfig
ge_options = {
  "ge.flowGraphMemMaxSize": "4106127360"
}
llm_config = LLMConfig()
llm_config.ge_options = ge_options
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None

## Listen\_ip\_info

**Function**

Sets cluster listening information. It corresponds to underlying llm.listenIpInfo configuration item.

**Prototype**

```
listen_ip_info(listen_ip_info)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| listen_ip_info | str | Sets the Host-side IP address and port. One value can be configured, for example, "192.168.1.1:26000". |

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.listen_ip_info = "192.168.1.1:26000"
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None

## Enable\_cache\_manager

**Function**

Configures whether to enable CacheManager mode. It corresponds to underlying llm.EnableCacheManager configuration item.

**Prototype**

```
enable_cache_manager(self, enable_cache_manager: bool)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| enable_cache_manager | bool | Specifies whether to enable CacheManager mode. It must be enabled.<br><br> - True: enabled.<br> - False: disabled. If not configured, it is disabled by default. |

In the Ascend 950PR/Ascend 950DT scenario, False is not supported.

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_cache_manager = True
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None

## Enable\_remote\_cache\_accessible

**Function**

Configures whether to enable direct access to remote Cache.

After this option is enabled, remote Cache metadata, such as indexes and memory addresses, is cached locally to speed up the Pull process. It also introduces related constraints. See "Constraints" for details.

**Prototype**

```
enable_remote_cache_accessible(self, enable_remote_cache_accessible: bool)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| enable_remote_cache_accessible | bool | Specifies whether to enable direct access to remote Cache. Values are as follows.<br><br> - True: enabled<br> - False: disabled<br><br>It is disabled by default.<br>If this option is disabled, Atlas A3 training series products/Atlas A3 inference series products support only the RDMA transfer protocol. You are advised to enable this option to support more transfer protocols. |

If this option is disabled, Atlas A3 training series products/Atlas A3 inference series products support only the RDMA transfer protocol. You are advised to enable this option to support more transfer protocols.
<br>In the Ascend 950PR/Ascend 950DT scenario, False is not supported.

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_remote_cache_accessible = True
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

The index is updated only during the first Pull. Cache operations performed by the peer after the first Pull are invisible to the local end. Users need to ensure Cache validity. Therefore, this option is more suitable for PA scenarios, because in these scenarios Cache is allocated or registered only during user script initialization and does not change frequently.

## Rdma\_traffic\_class

**Function**

Used to configure the traffic class of the RDMA NIC.

**Prototype**

```
rdma_traffic_class(self, rdma_traffic_class: int)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| rdma_traffic_class | int | Configures the traffic class of the RDMA NIC. It has the same function as the HCCL_RDMA_TC environment variable. If both are configured, this option has higher priority. If only one is configured, that configuration takes effect.<br>The value range is [0,255]. The value must be a multiple of 4. The default value is 132. |

For the environment variables in the preceding table, see [Environment Variable Reference](https://www.hiascend.com/document/redirect/CannCommunityEnvRef).

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_traffic_class = 100
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None.

## Rdma\_service\_level

**Function**

Used to configure the service level of the RDMA NIC.

**Prototype**

```
rdma_service_level(self, rdma_service_level: int)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| rdma_service_level | int | Configures the service level of the RDMA NIC. It has the same function as the HCCL_RDMA_SL environment variable. If both are configured, this option has higher priority. If only one is configured, that configuration takes effect.<br>The value range is [0, 7]. The default value is 4. |

For the environment variables in the preceding table, see [Environment Variable Reference](https://www.hiascend.com/document/redirect/CannCommunityEnvRef).

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_service_level = 2
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None.

## Local\_comm\_res

**Function**

Used to configure s local communication resources.

**Prototype**

```
local_comm_res(local_comm_res)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| local_comm_res | str | Configures local communication resource information in JSON string format. You only need to configure the Device information used by the current llm datadist in the ranktable. The server_count and rank_id fields in the ranktable do not need to be configured. If enable_cache_manager and enable_remote_cache_accessible are not configured, they default to True after this option is configured.

In Atlas A2 training series products/Atlas A2 inference series products scenarios, you only need to configure the Device information used by the current llm datadist in the ranktable. The server_count and rank_id fields in the ranktable do not need to be configured. For details about ranktable, see [HCCL Collective Communication Library User Guide](https://gitcode.com/cann/hixl/issues/38). The configuration example is as follows.

```
{
  "server_list": [
    {
      "device": [
        {
          "device_id": "0",
          "device_ip": "x.x.x.x"
        },
      ],
      "server_id": "xxxx"
    }
  ],
  "status": "completed",
  "version": "1.0"
}
```

This field can be configured as empty. The example is as follows.

```
local_comm_res = ""
```

In Atlas A3 training series products/Atlas A3 inference series products scenarios, you only need to configure the Device information used by the current llm datadist in the ranktable. The server_count and rank_id fields in the ranktable do not need to be configured. For details about ranktable, see [HCCL Collective Communication Library User Guide](https://www.hiascend.com/document/redirect/CannCommunityHcclUg). The configuration example is as follows.

```
{
  "server_list": [
    {
      "device": [
        {
          "device_id": "0",
          "device_ip": "x.x.x.x"
        },
      ],
      "server_id": "xxxx"
    }
  ],
  "status": "completed",
  "version": "1.0"
}
```

This field can be configured as empty. The example is as follows.

```
local_comm_res = ""
```

For the configuration format in Ascend 950PR/Ascend 950DT scenarios, see [gitcode](https://gitcode.com/cann/hixl/issues/38). transfer_backend must also be enabled as the hixl transfer backend. Empty configuration is not supported.

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.local_comm_res = '''{
  "status": "completed",
  "version": "1.0",
  "server_list": [
    {
      "server_id": "node_0",
      "device": [
        {
          "device_id": "0",
          "device_ip": "x.x.x.x"
        }
      ]
    }
  ]
}'''
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

After this option is configured, the following constraints apply.

- When calling link\_clusters or unlink\_clusters, if local\_comm\_res is not empty, the append\_local\_ip\_info information in the clusters parameter does not need to be configured. If local\_comm\_res is empty, the append\_local\_ip\_info information in the clusters parameter must be configured.
- Scenarios where enable\_cache\_manager and enable\_remote\_cache\_accessible are configured as "False" are not supported.

## Link\_total\_time

**Function**

Used to configure the total timeout for HCCL link establishment failures.

**Prototype**

```
link_total_time(self, link_total_time: int)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| link_total_time | int | Configures the HCCL link establishment timeout, in seconds. Retries are performed based on the retry count within the total timeout.<br>The value range is [0, 2^32-1]. The default value is 0. This parameter can be left unconfigured. If it is not configured, 0 is passed automatically. After 0 is passed, HCCL_CONNECT_TIMEOUT is read automatically. The default value is 120s. |

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_total_time = 20
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None.

## Link\_retry\_count

**Function**

Used to configure the retry count for HCCL link establishment failures.

**Prototype**

```
link_retry_count(link_retry_count)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| link_retry_count | int | Configures the retry count for HCCL link establishment failures. The unit is times. Retries are performed based on the retry count within the total timeout.<br>The value range is [1, 100]. The default value is 1. This parameter can be left unconfigured. If it is not configured, 1 is passed automatically. |

**Example**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_retry_count = 2
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

None

## Transfer\_backend

**Function**

Used to configure the transfer backend used by LLM-DataDist.

**Prototype**

```
transfer_backend(transfer_backend)
```

**Parameters**

| Parameter Name | Data Type | Value Description |
| --- | --- | --- |
| transfer_backend | str | The value is "hixl", which specifies hixl as the transfer engine backend. |

**Example**

```
ffrom llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.transfer_backend = "hixl"
```

**Return Value**

In normal cases: No return value.

Parameter errors may throw TypeError or ValueError.

**Constraints**

- The initialization options must specify listen_ip_info. When the hixl transfer backend is configured, each transfer end can be either a Client or a Server.
- Before initiating transfer with the peer, call link_clusters to initiate link establishment.
