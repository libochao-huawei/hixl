# LLM-DataDist Data Structures

## LlmRole

LLM-DataDist roles

```
enum class LlmRole : int32_t {
  kPrompt = 1,      // Role: Prompt
  kDecoder = 2,     // Role: Decoder
  kMix = 3,         // Role: Mix
  kEnd              // Invalid value
}
```

## CachePlacement

Cache types

```
enum class CachePlacement : uint32_t {
  kHost = 0U,             // Cache: host memory
  kDevice = 1U,           // Cache: device memory
}
```

## CacheDesc

Cache description

```
struct CacheDesc {
  CachePlacement placement = CachePlacement::kDevice;    // Memory type
  uint32_t num_tensors = 0U;                             // Number of tensors in the cache
  DataType data_type = DT_UNDEFINED;                     // Data type of the tensor in the cache
  std::vector<int64_t> shape;                            // Shape of the tensor in the cache
  uint8_t reserved[128];                                 // Reserved
}
```

## CacheIndex

Cache index

```
struct CacheIndex {
  uint64_t cluster_id;        // ID of the cluster where the cache is located
  int64_t cache_id;           // Cache ID
  uint32_t batch_index;       // Index of the batch used for PullKvCache
  uint8_t reserved[128];      // Reserved
}
```

## Cache

The cache maintains a group of tensor addresses.

```
struct Cache {
  int64_t cache_id = -1;                     // Cache ID
  std::vector<uintptr_t> tensor_addrs;       // Addresses of tensors in the cache. In the single-process multi-device scenario, the addresses of multiple devices are arranged in sequence.
  CacheDesc cache_desc;                      // Cache description
  uint8_t reserved[128];                     // Reserved
}
```

## ClusterInfo and IpInfo

Cluster information used for link establishment and disconnection.

```
struct ClusterInfo {
  uint64_t remote_cluster_id = 0U;     // Cluster ID of the peer LLM-DataDist
  int32_t remote_role_type = 0;          // role_type of the peer LLM-DataDist. The value 0 indicates the full LLM-DataDist, while the value 1 indicates the incremental LLM-DataDist.
  std::vector<IpInfo> local_ip_infos;  // IP address of the local LLM-DataDist. For details, see the following IpInfo structure.
  std::vector<IpInfo> remote_ip_infos; // IP address of the peer LLM-DataDist. For details, see the following IpInfo structure.
  uint8_t reserved[128];               // Reserved
}

struct IpInfo {
  AscendString ip;         // IP address
  uint16_t port = 0U;      // Port number
  uint8_t reserved[128];   // Reserved
}
```

## KvCacheExtParam

Extended parameters passed when the Pull or Push API is called.

```
struct KvCacheExtParam {
  std::pair<int32_t, int32_t> src_layer_range = {-1, -1};  // Layer range at the source side during KV transmission
  std::pair<int32_t, int32_t> dst_layer_range{-1, -1};  // Layer range at the destination side during KV transmission
  uint8_t tensor_num_per_layer = 2U;                       // Number of tensors per layer during KV transmission
  uint8_t reserved[127];                                   // Reserved
}
```

## RegisterCfg

Configuration parameters passed when the RegisterKvCache API is called.

```
struct RegisterCfg {
  uint8_t reserved[128] = {0}; // Reserved
}
```
