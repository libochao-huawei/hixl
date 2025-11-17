#include "hccl_types.h"
#include "HixlCsClient.h"

/**
 * @brief 注册Client端内存
 */
HcclResult HixlCsClientRegMem(ClientHandle client_handle, const char *mem_tag, const HcclMem *mem, void** mem_handle) {
  if (client_handle == nullptr) {
    return HCCL_E_PTR;
  }
  
  HixlCsClient* client = static_cast<HixlCsClient*>(client_handle);
  return client->ClientRegMem(&client_handle, mem_tag, mem, mem_handle);
}

/**
 * @brief 注销Client端内存
 */
HcclResult HixlCsClientUnregMem(ClientHandle client_handle, void** mem_handle) {
  if (client_handle == nullptr) {
    return HCCL_E_PTR;
  }
  
  HixlCsClient* client = static_cast<HixlCsClient*>(client_handle);
  return client->ClientUnregMem(&client_handle, &mem_handle);
}

/**
 * @brief Client端批量写入数据到Server端
 */
HcclResult HixlCsClientBatchput(void *client_handle, uint32_t list_num, void **remote_buf_list, 
                                const void **local_buf_list, uint64_t *len_list, void **complete_handle) {
  if (client_handle == nullptr) {
    return HCCL_E_PTR;
  }
  
  HixlCsClient* client = static_cast<HixlCsClient*>(client_handle);
  return client->BatchPut(client_handle, list_num, remote_buf_list, 
                                local_buf_list, len_list, complete_handle);
}

/**
 * @brief Client端批量从Server端读取数据
 */
HcclResult HixlCsClientBatchget(void *client_handle, uint32_t list_num, void **remote_buf_list, 
                                const void **local_buf_list, uint64_t *len_list, void **complete_handle) {
  if (client_handle == nullptr) {
    return HCCL_E_PTR;
  }
  
  HixlCsClient* client = static_cast<HixlCsClient*>(client_handle);
  return client->BatchGet(client_handle, list_num, remote_buf_list, 
                                local_buf_list, len_list, complete_handle);
}

/**
 * @brief 查询批量操作的完成状态
 */
HcclResult HixlCsClientQueryCompleteStatus(void *client_handle, void *complete_handle, int32_t *status) {
  if (client_handle == nullptr) {
    return HCCL_E_PTR;
  }
  
  HixlCsClient* client = static_cast<HixlCsClient*>(client_handle);
  return client->QueryCompleteStatus(client_handle, complete_handle, status);
}
