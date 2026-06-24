# LLMClusterInfo

## Supported Products

| Product| Supported|
| --- | --- |
| Ascend 950PR and Ascend 950DT| √ |
| Atlas A3 training products and Atlas A3 inference products| √ |
| Atlas A2 training products and Atlas A2 inference products| √ |

Note: For Atlas A2 training products and Atlas A2 inference products, only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.

## LLMClusterInfo Constructor

**Function**

It constructs `LLMClusterInfo`, which is used as the parameter type of the `link_clusters` and `unlink_clusters` APIs.

**Prototype**

```
__init__()
```

**Description**

None

**Example**

```
from llm_datadist import LLMClusterInfo
llm_cluster = LLMClusterInfo()
```

**Returns**

LLMClusterInfo instances are returned.

**Constraints**

None

## remote\_cluster\_id

**Function**

It sets the peer cluster ID.

**Prototype**

```
remote_cluster_id(remote_cluster_id)
```

**Description**

| Parameter| Data Type| Value|
| --- | --- | --- |
| remote_cluster_id | int | ID of the peer cluster.|

**Example**

```
llm_cluster = LLMClusterInfo()
llm_cluster.remote_cluster_id = 1
```

**Returns**

In normal cases, no value is returned.

If a parameter is incorrect, `TypeError` or `ValueError` may be reported.

**Constraints**

None

## append\_local\_ip\_info

**Function**

It adds the IP address of the local cluster.

**Prototype**

```
append_local_ip_info(self, ip: Union[str, int], port: int)
```

**Description**

| Parameter| Data Type| Value|
| --- | --- | --- |
| ip | Union[str, int] | IP address of the host in the local cluster.|
| port | int | Port number of the host in the local cluster.|

**Example**

```
llm_cluster = LLMClusterInfo()
llm_cluster.append_local_ip_info("1.1.1.1", 10000)
```

**Returns**

In normal cases, no value is returned.

If a parameter is incorrect, `TypeError` or `ValueError` may be reported.

**Constraints**

None

## append\_remote\_ip\_info

**Function**

It adds the IP address of the remote cluster.

**Prototype**

```
append_remote_ip_info(self, ip: Union[str, int], port: int)
```

**Description**

| Parameter| Data Type| Value|
| --- | --- | --- |
| ip | Union[str, int] | IP address of the host.|
| port | int | Port number of the host.|

**Example**

```
llm_cluster = LLMClusterInfo()
llm_cluster.append_remote_ip_info("1.1.1.1", 10000)
```

**Returns**

In normal cases, no value is returned.

If a parameter is incorrect, `TypeError` or `ValueError` may be reported.

**Constraints**

None
