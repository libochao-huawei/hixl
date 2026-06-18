# LLM-DataDist Error Codes

Error codes are defined by the following macros:

```
namespace llm_datadist {
constexpr Status LLM_SUCCESS = 0x0U;
constexpr Status LLM_FAILED = 0xFFFFFFFFU;
constexpr Status LLM_WAIT_PROC_TIMEOUT = 0x5010B001U;
constexpr Status LLM_KV_CACHE_NOT_EXIST = 0x5010B002U;
constexpr Status LLM_PARAM_INVALID = 0x5010B005U;
constexpr Status LLM_NOT_YET_LINK = 0x5010B007U;
constexpr Status LLM_ALREADY_LINK = 0x5010B008U;
constexpr Status LLM_LINK_FAILED = 0x5010B009U;
constexpr Status LLM_UNLINK_FAILED = 0x5010B00AU;
constexpr Status LLM_NOTIFY_PROMPT_UNLINK_FAILED = 0x5010B00BU;
constexpr Status LLM_CLUSTER_NUM_EXCEED_LIMIT = 0x5010B00CU;
constexpr Status LLM_PROCESSING_LINK = 0x5010B00DU;
constexpr Status LLM_DEVICE_OUT_OF_MEMORY = 0x5010B00EU;
constexpr Status LLM_EXIST_LINK = 0x5010B018U;
constexpr Status LLM_FEATURE_NOT_ENABLED = 0x5010B019U;
constexpr Status LLM_TIMEOUT = 0x5010B01AU;
constexpr Status LLM_LINK_BUSY = 0x5010B01BU;
constexpr Status LLM_OUT_OF_MEMORY = 0x5010B01CU;
}  // namespace llm_datadist
```

The meanings of error codes are as follows.

| Enumerated Value| Meaning| Recoverable| Solution|
| --- | --- | --- | --- |
| LLM_SUCCESS | Successful| N/A| None|
| LLM_FAILED | Common failure| No| Preserve the environment, collect Host/Device logs, and back them up.|
| LLM_WAIT_PROC_TIMEOUT | Process timed out| Yes| - If this error is reported by transmission-related APIs such as `PullKvCache` and `PullKvBlocks`, the link cannot be recovered and needs to be re-established.<br>  - If this error is reported by other APIs, increase the timeout interval and try again.|
| LLM_KV_CACHE_NOT_EXIST | KV not found| Yes| - Check whether `cache_id` is correct.<br>  - Check whether the cache has been released.<br>  - Check whether the request in the full error log is complete.<br>  - Check whether repeated pulling exists.|
| LLM_PARAM_INVALID | Incorrect parameters| Yes| Locate the fault based on logs.|
| LLM_NOT_YET_LINK | No link established| Yes| Check the link establishment between Decode and Prompt at the upper layer.|
| LLM_ALREADY_LINK | Link already established| Yes| Check the link establishment between Decode and Prompt at the upper layer.|
| LLM_LINK_FAILED | Link setup failure| Yes| If this error code is returned in the second return value of `LinkLlmClusters`, check the network connectivity between the corresponding clusters.|
| LLM_UNLINK_FAILED | Link disconnection failure| Yes| If this error code is returned in the second return value of `UnlinkLlmClusters`, check the network connectivity between the corresponding clusters.|
| LLM_NOTIFY_PROMPT_UNLINK_FAILED | Failed to notify Prompt of link disconnection| Yes| 1. Check the network connectivity between Decode and Prompt.<br>  2. Call the `UnlinkLlmClusters` API on the Prompt side to clean up residual resources.|
| LLM_CLUSTER_NUM_EXCEED_LIMIT | Too many clusters| Yes| Check the input parameters of `LinkLlmClusters` and `UnlinkLlmClusters`, and ensure that the number of clusters does not exceed `16`.|
| LLM_PROCESSING_LINK | Link setup in progress| Yes| A link establishment or disconnection operation is in progress. Try again later.|
| LLM_DEVICE_OUT_OF_MEMORY | Insufficient device memory| Yes| Check whether the allocated memory is released.|
| LLM_EXIST_LINK | Unreleased links exist when setting the role| Yes| Check whether `UnlinkLlmClusters` is called to disconnect all links before `SetRole` is called.|
| LLM_FEATURE_NOT_ENABLED | Feature not enabled| Yes| Check whether necessary options are input during LLM-DataDist initialization.<br>Check whether unsupported APIs are called.|
| LLM_TIMEOUT | Process timed out| No| Preserve the environment, collect Host/Device logs, and back them up.|
| LLM_LINK_BUSY | Link busy| Yes| Reserved error code. It will not be returned.|
| LLM_OUT_OF_MEMORY | Insufficient memory| Yes| Check whether the memory pool or system memory is sufficient.|
