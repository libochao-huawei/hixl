#include "HixlMemStore.h"
#include <cstring>

HixlMemStore::HixlMemStore() 
    : server_id_counter_(0), 
      client_id_counter_(0) {
}

HixlMemStore::~HixlMemStore() {
}

HcclResult HixlMemStore::RegisterServerMemory(ServerHandle* server_handle, void* addr, size_t size) {
  if (server_handle == nullptr || addr == nullptr || size == 0) {
    return HCCL_E_PARA;
  }

  std::lock_guard<std::mutex> lock(server_mutex_);
  
  uint64_t id = ++server_id_counter_;
  server_regions_[id] = MemoryRegion(addr, size);
  
  return HCCL_SUCCESS;
}

HcclResult HixlMemStore::RegisterClientMemory(ClientHandle* client_handle, void* addr, size_t size) {
  if (client_handle == nullptr || addr == nullptr || size == 0) {
    return HCCL_E_PARA;
  }

  std::lock_guard<std::mutex> lock(client_mutex_);
  
  uint64_t id = ++client_id_counter_;
  client_regions_[id] = MemoryRegion(addr, size);
  
  return HCCL_SUCCESS;
}

HcclResult HixlMemStore::UnregisterServerMemory(ServerHandle* server_handle) {
  if (server_handle == nullptr) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(server_mutex_);
  
  // 查找并删除对应的内存区域
  for (auto it = server_regions_.begin(); it != server_regions_.end(); ++it) {
    // 这里需要根据server_handle找到对应的内存区域ID
    // 简化实现，实际需要根据具体数据结构来查找
  }
  
  return HCCL_SUCCESS;
}

HcclResult HixlMemStore::UnregisterClientMemory(ClientHandle* client_handle) {
  if (client_handle == nullptr) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(client_mutex_);
  
  // 查找并删除对应的内存区域
  for (auto it = client_regions_.begin(); it != client_regions_.end(); ++it) {
    // 查找并删除对应的内存区域
    // 简化实现，实际需要根据具体数据结构来查找
  }
  
  return HCCL_SUCCESS;
}

HcclResult HixlMemStore::ValidateMemoryAccess(void* server_addr, size_t server_size, 
                                   void* client_addr, size_t client_size) {
  if (server_addr == nullptr || client_addr == nullptr || 
      server_size == 0 || client_size == 0) {
    return HCCL_E_PARA;
  }

  bool server_valid = false;
  bool client_valid = false;

  // 验证Server端内存访问
  {
    std::lock_guard<std::mutex> lock(server_mutex_);
    for (const auto& region : server_regions_) {
      if (region.second.Contains(server_addr, server_size)) {
      server_valid = true;
      break;
      }
    }
  }

  // 验证Client端内存访问
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (const auto& region : client_regions_) {
      if (region.second.Contains(client_addr, client_size)) {
      client_valid = true;
      break;
      }
    }
  }

  if (!server_valid || !client_valid) {
    return HCCL_E_UNAVAIL; // 内存未注册或访问越界
  }

  return HCCL_SUCCESS;
}
