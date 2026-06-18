# API Reference (Python)

- The following products are supported:

  - Atlas A2 training products/Atlas A2 inference products: Only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported. When the HCCS transmission protocol is used in the server, only D2D transmission is supported. A maximum of 50 GB device memory and 20 GB host memory can be registered. The larger the registered memory is, the more the OS memory is occupied.
  - Atlas A3 training products/Atlas A3 inference products: When the HCCS transmission protocol is used, the host memory cannot be used as the remote cache. A maximum of 50 GB device memory and 20 GB host memory can be registered. The larger the registered memory is, the more the OS memory is occupied.
  - Ascend 950PR/Ascend 950DT: The UB protocol is used within a SuperPoD, and the RoCE protocol is used between SuperPoDs.
- Currently, Python 3.9 to 12 are supported. For details about how to install Python, visit the [Python official website](https://www.python.org/).
- A maximum of 50 GB device memory and 20 GB host memory can be registered. The larger the registered memory is, the more the OS memory is occupied. The following products are supported:
  - Atlas A2 training products/Atlas A2 inference products: Only the Atlas 800I A2 inference server and A200I A2 Box heterogeneous subrack are supported.
  - Atlas A3 training products/Atlas A3 inference products

The API list is as follows.

- [LLMDataDist](LLMDataDist.md)
- [LLMConfig](LLMConfig.md)
- [CacheManager](CacheManager.md)
- [Cache](Cache.md)
- [LLMRole](LLMRole.md)
- [LLMClusterInfo](LLMClusterInfo.md)
- [RegisterMemStatus](RegisterMemStatus.md)
- [Placement](Placement.md)
- [CacheDesc](CacheDesc.md)
- [MemType](MemType.md)
- [MemInfo](MemInfo.md)
- [CacheKey](CacheKey.md)
- [CacheKeyByIdAndIndex](CacheKeyByIdAndIndex.md)
- [BlocksCacheKey](BlocksCacheKey.md)
- [LayerSynchronizer](LayerSynchronizer.md)
- [TransferConfig](TransferConfig.md)
- [TransferWithCacheKeyConfig](TransferWithCacheKeyConfig.md)
- [CacheTask](CacheTask.md)
- [LLMException](LLMException.md)
- [LLMStatusCode](LLMStatusCode.md)
- [DataType](DataType.md)
- [To Be Discarded](To Be Discarded.md)
