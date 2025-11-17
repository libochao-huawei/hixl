#include "HixlCsClient.h"
#include "HixlMemStore.h"
#include "EndpointStore.h"
#include <cstring>
#include <algorithm>

// 全局EndpointStore实例
extern EndpointStore g_endpoint_store;
// 全局HixlMemStore实例  
extern HixlMemStore g_mem_store;

HixlCsClient::HixlCsClient() 
    : client_ip_(""), 
      client_port_(0), 
      is_connected_(false),
      server_id_counter_(0),
      client_id_counter_(0) {
}

HixlCsClient::~HixlCsClient() {
}

HcclResult HixlCsClient::Create(char* server_ip, uint32_t server_port, 
                              const EndPoint* src_end_point, const EndPoint* dst_end_point, 
                              ClientHandle* client_handle) {
  if (server_ip == nullptr || src_end_point == nullptr || 
      dst_end_point == nullptr || client_handle == nullptr) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  
  client_ip_ = server_ip;
  client_port_ = server_port;
  src_end_point_ = *src_end_point;
  dst_end_point_ = *dst_end_point;
  
  *client_handle = static_cast<ClientHandle>(this);
  
  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::Connect(ClientHandle* client_handle) {
  if (client_handle == nullptr || *client_handle != static_cast<ClientHandle>(this)) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  
  if (is_connected_) {
    return HCCL_SUCCESS;
  }

  // 创建端点
  EndpointHandle endpoint_handle = nullptr;
  HcclResult result = g_endpoint_store.CreateEndpoint(src_end_point_, &endpoint_handle);
  if (result != HCCL_SUCCESS) {
    return result;
  }

  // 创建通道
  ChannelHandle channel_handle = nullptr;
  result = g_endpoint_store.CreateChannel(endpoint_handle, &channel_handle);
  if (result != HCCL_SUCCESS) {
    return result;
  }

  is_connected_ = true;
  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::GetStatus(ClientHandle* client_handle, int32_t* status) {
  if (client_handle == nullptr || *client_handle != static_cast<ClientHandle>(this)) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  
  *status = is_connected_ ? 1 : 0;
  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::ClientRegMem(ClientHandle* client_handle, const char *mem_tag, const HcclMem *mem, MemHandle *mem_handle) {
  if (client_handle == nullptr || *client_handle != static_cast<ClientHandle>(this)) {
    return HCCL_E_PTR;
  }

  if (mem_tag == nullptr || mem == nullptr || mem_handle == nullptr) {
    return HCCL_E_PTR;
  }

  if (mem->addr == nullptr || mem->size == 0) {
    return HCCL_E_PARA;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 获取Endpoint句柄
  std::vector<EndpointHandle> endpoint_handles = g_endpoint_store.GetAllEndpointHandles();
  if (endpoint_handles.empty()) {
    return HCCL_E_NOT_FOUND;
  }

  EndpointHandle endpoint_handle = endpoint_handles[0];

  // 调用EndpointStore注册内存
  HcclResult result = g_endpoint_store.RegisterMem(endpoint_handle, mem_tag, mem, mem_handle);
  if (result != HCCL_SUCCESS) {
    return result;
  }

  // 保存内存信息到EndpointInfo
  EndpointMemInfo mem_info;
  mem_info.endpoint_handle = endpoint_handle;
  mem_info.mem_handle = *mem_handle;
  mem_info.mem_tag = mem_tag;
  
  remote_mem_info_[*mem_handle].push_back(mem_info);

  // 注册到HixlMemStore
  result = g_mem_store.RegisterClientMemory(client_handle, mem->addr, mem->size);
  if (result != HCCL_SUCCESS) {
    // 回滚操作
    g_endpoint_store.DeregisterMem(endpoint_handle, *mem_handle);
    remote_mem_info_.erase(*mem_handle);
    return result;
  }

  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::GetRemoteMem(ClientHandle* client_handle, HcclMem** remote_mem_list, 
                            char** mem_tag_list, uint32_t* list_num, uint32_t timeout) {
  // 实现获取远端内存信息的逻辑
  // 这里需要根据具体实现来完善
  return HCCL_E_NOT_SUPPORT;
}

HcclResult HixlCsClient::ClientUnregMem(ClientHandle* client_handle, MemHandle* mem_handle) {
  if (client_handle == nullptr || *client_handle != static_cast<ClientHandle>(this)) {
    return HCCL_E_PTR;
  }

  if (mem_handle == nullptr) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 获取Endpoint句柄
  std::vector<EndpointHandle> endpoint_handles = g_endpoint_store.GetAllEndpointHandles();
  if (endpoint_handles.empty()) {
    return HCCL_E_NOT_FOUND;
  }

  EndpointHandle endpoint_handle = endpoint_handles[0];

  // 调用EndpointStore注销内存
  HcclResult result = g_endpoint_store.DeregisterMem(endpoint_handle, *mem_handle);
  if (result != HCCL_SUCCESS) {
    return result;
  }

  // 从HixlMemStore注销
  result = g_mem_store.UnregisterClientMemory(client_handle);
  if (result != HCCL_SUCCESS) {
    return result;
  }

  // 删除本地保存的内存信息
  remote_mem_info_.erase(*mem_handle);

  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::BatchPut(void* client_handle, uint32_t list_num, void** remote_buf_list, 
                            const void** local_buf_list, uint64_t* len_list, void** complete_handle) {
  if (client_handle == nullptr || remote_buf_list == nullptr || 
      local_buf_list == nullptr || len_list == nullptr || complete_handle == nullptr) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 第一阶段：地址校验
  for (uint32_t i = 0; i < list_num; ++i) {
    HcclResult validate_result = g_mem_store.ValidateMemoryAccess(
        remote_buf_list[i], len_list[i], 
        const_cast<void*>(local_buf_list[i]), len_list[i]);
    
    if (validate_result != HCCL_SUCCESS) {
      return validate_result;
    }
  }

  // 第二阶段：批量写入
  for (uint32_t i = 0; i < list_num; ++i) {
    // 调用EndpointStore进行写入操作
    // 这里需要根据具体实现来完善
  }

  // 生成complete_handle
  *complete_handle = static_cast<void*>(this); // 简化实现

  std::lock_guard<std::mutex> complete_lock(complete_mutex_);
  complete_handles_[*complete_handle] = 0; // 初始状态为未完成

  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::BatchGet(void* client_handle, uint32_t list_num, void** remote_buf_list, 
                            const void** local_buf_list, uint64_t* len_list, void** complete_handle) {
  if (client_handle == nullptr || remote_buf_list == nullptr || 
      local_buf_list == nullptr || len_list == nullptr || complete_handle == nullptr) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 地址校验
  for (uint32_t i = 0; i < list_num; ++i) {
    HcclResult validate_result = g_mem_store.ValidateMemoryAccess(
        remote_buf_list[i], len_list[i], 
        const_cast<void*>(local_buf_list[i]), len_list[i]);
    
    if (validate_result != HCCL_SUCCESS) {
      return validate_result;
    }
  }

  // 批量读取
  for (uint32_t i = 0; i < list_num; ++i) {
    // 调用EndpointStore进行读取操作
    // 这里需要根据具体实现来完善
  }

  // 生成complete_handle
  *complete_handle = static_cast<void*>(this); // 简化实现

  std::lock_guard<std::mutex> complete_lock(complete_mutex_);
  complete_handles_[*complete_handle] = 0; // 初始状态为未完成

  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::QueryCompleteStatus(void* client_handle, void* complete_handle, int32_t* status) {
  if (client_handle == nullptr || complete_handle == nullptr || status == nullptr) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(complete_mutex_);
  
  auto it = complete_handles_.find(complete_handle);
  if (it == complete_handles_.end()) {
    return HCCL_E_NOT_FOUND;
  }

  *status = it->second;
  return HCCL_SUCCESS;
}

HcclResult HixlCsClient::Destroy(ClientHandle* client_handle) {
  if (client_handle == nullptr || *client_handle != static_cast<ClientHandle>(this)) {
    return HCCL_E_PTR;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  
  // 清理所有完成句柄
  {
    std::lock_guard<std::mutex> complete_lock(complete_mutex_);
    complete_handles_.clear();
  }

  remote_mem_info_.clear();
  channels_.clear();
  is_connected_ = false;

  return HCCL_SUCCESS;
}
