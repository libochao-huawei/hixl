# LLMStatusCode

The following table lists the enumeration values and solutions corresponding to `status_code` in `LLMException`.

| Enumerated Value| Meaning| Recoverable| Solution|
| --- | --- | --- | --- |
| LLM_SUCCESS | Successful| N/A| None|
| LLM_FAILED | Common failure| No| Restart the host or container.<br>Preserve the environment, collect Host/Device logs, and back them up.|
| LLM_PARAM_INVALID | Incorrect parameters| Yes| Locate the fault based on logs.|
| LLM_KV_CACHE_NOT_EXIST | KV not found| Yes| - Check whether the request in the full error log is complete.<br>  - Check whether repeated pulling exists.<br>  - Check whether the parameters for marking the target cache are incorrect.|
| LLM_REPEAT_REQUEST | Duplicate request| Yes| Check whether repeated calls exist.|
| LLM_NOT_YET_LINK | No link established| Yes| Check the link establishment between Decode and Prompt at the upper layer.|
| LLM_ALREADY_LINK | A link has been established.| Yes| Check the link establishment between Decode and Prompt at the upper layer.|
| LLM_LINK_FAILED | Link setup failure| Yes| If this error code is returned in the second return value of `link_clusters`, check the network connectivity between the corresponding clusters.|
| LLM_UNLINK_FAILED | Link disconnection failure| Yes| If this error code is returned in the second return value of `unlink_clusters`, check the network connectivity between the corresponding clusters.|
| LLM_NOTIFY_PROMPT_UNLINK_FAILED | Failed to notify Prompt of link disconnection| Yes| 1. Check the network connectivity between Decode and Prompt.<br>  2. Call the `unlink_clusters` API on the Prompt side to clean up residual resources.|
| LLM_CLUSTER_NUM_EXCEED_LIMIT | Too many clusters| Yes| Check the input parameters of `link_clusters` and `unlink_clusters`, and ensure that the number of clusters does not exceed `16`.|
| LLM_PROCESSING_LINK | Link setup in progress| Yes| A link establishment or disconnection operation is in progress. Try again later.|
| LLM_PREFIX_ALREADY_EXIST | Prefix already exists| Yes| Check whether a public prefix with the same prefix ID has been loaded. If yes, release it first.|
| LLM_PREFIX_NOT_EXIST | Prefix not found| Yes| Check whether the prefix ID in the request has been loaded.|
| LLM_EXIST_LINK | Unreleased links exist during the `switch_role` operation.| Yes| Check whether `unlink_clusters` is called to disconnect all links before the role of the current LLMDataDist instance is switched.|
| LLM_FEATURE_NOT_ENABLED | Feature not enabled| Yes| Check whether necessary options are input during LLMDataDist initialization.<br>If this exception is thrown during the role switching of the current LLMDataDist instance, check whether `enable_switch_role` is set to `True` in LLMConfig during initialization.|
| LLM_TIMEOUT | Process timed out| Yes| - If this error is reported by transmission-related APIs such as `pull_cache`, `pull_blocks`, and `transfer_cache_async`, the link cannot be recovered and needs to be re-established.<br>  - If this error is reported by other APIs, increase the timeout interval and try again.|
| LLM_LINK_BUSY | Link busy| Yes| Check whether the APIs called at the same time conflict with each other. For example, this error code is reported when the following APIs are called at the same time:<br>Both `unlink` and `pull_cache` are called at the same time.<br>Both `pull_cache` and `transfer_cache_async` are called at the same time using the same link.|
| LLM_OUT_OF_MEMORY | Insufficient memory| Yes| Check whether the memory pool is sufficient for the requested KV size.<br>Check whether the allocated memory is released.|
| LLM_DEVICE_MEM_ERROR | Virtual addresses where UCE (error that cannot be directly handled by the system hardware) occurs| Yes| Obtain and fix the virtual addresses where memory UCE occurs. If it is a KV cache memory, additionally call the `remap_registered_memory` API of CacheManager to restore the KV cache memory registered with the NIC.<br> Note: This error code is reserved and not supported currently.|
| LLM_SUSPECT_REMOTE_ERROR | Suspected UCE memory fault| No| The upper-layer framework needs to determine whether the fault is a UCE memory fault or another type of fault.|
| LLM_UNKNOWN_ERROR | Unknown error| No| Preserve the environment, collect Host/Device logs, and back them up.|

Obtain and fix the incorrect virtual address of the memory UCE by referring to the description of the `torch_npu.npu.restart_device` API in [PyTorch](https://www.hiascend.com/en/developer/software/ai-frameworks/pytorch).
