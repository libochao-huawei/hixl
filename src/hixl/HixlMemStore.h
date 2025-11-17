#ifndef HCCL_MEM_STORE_H_
#define HCCL_MEM_STORE_H_

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include "hccl_types.h"

/**
 * @brief 内存存储管理类
 * 
 * 负责管理Server和Client端注册的内存区域，提供内存访问验证功能
 */
class ASCEND_FUNC_VISIBILITY HixlMemStore {
 public:
  HixlMemStore();
  ~HixlMemStore();

  /**
   * @brief 注册Server端内存区域
   * @param server_handle 服务端句柄
   * @param addr 要注册的内存起始地址
   * @param size 要注册的内存区域大小
   * @return 操作结果
   */
  HcclResult RegisterServerMemory(ServerHandle* server_handle, void* addr, size_t size);
  
  /**
   * @brief 注册Client端内存区域
   * @param client_handle 客户端句柄
   * @param addr 要注册的内存起始地址
   * @param size 要注册的内存区域大小
   * @return 操作结果
   */
  HcclResult RegisterClientMemory(ClientHandle* client_handle, void* addr, size_t size);
  
  /**
   * @brief 注销Server端内存区域
   * @param server_handle 服务端句柄
   * @return 操作结果
   */
  HcclResult UnregisterServerMemory(ServerHandle* server_handle);
  
  /**
   * @brief 注销Client端内存区域
   * @param client_handle 客户端句柄
   * @return 操作结果
   */
  HcclResult UnregisterClientMemory(ClientHandle* client_handle);
  
  /**
   * @brief 验证Client对Server的内存访问请求是否在注册范围内
   * @param server_addr 请求访问的Server端内存地址
   * @param server_size 请求访问的Server端内存大小
   * @param client_addr 发起请求的Client端内存地址
   * @param client_size 发起请求的Client端内存大小
   * @return 验证结果
   */
  HcclResult ValidateMemoryAccess(void* server_addr, size_t server_size, 
                                 void* client_addr, size_t client_size);

 private:
  // 内存区域信息结构体
  struct MemoryRegion {
    void* addr;   // 内存起始地址
    size_t size;  // 内存区域大小
    
    MemoryRegion(void* a = nullptr, size_t s = 0) : addr(a), size(s) {}
    
    /**
     * @brief 检查指定地址和大小是否在当前内存区域内
     * @param check_addr 要检查的地址
     * @param check_size 要检查的大小
     * @return 是否包含在内
     */
    bool Contains(void* check_addr, size_t check_size) const {
      if (addr == nullptr || check_addr == nullptr) return false;
      return (check_addr >= addr) && 
             (static_cast<char*>(check_addr) + check_size <= static_cast<char*>(addr) + size;
    }
  };
  
  // Server内存区域映射表：ID -> MemoryRegion
  std::unordered_map<uint64_t, MemoryRegion> server_regions_;
  
  // Client内存区域映射表：ID -> MemoryRegion  
  std::unordered_map<uint64_t, MemoryRegion> client_regions_;
  
  // Server内存注册的互斥锁
  std::mutex server_mutex_;
  
  // Client内存注册的互斥锁
  std::mutex client_mutex_;
  
  // Server内存ID计数器
  uint64_t server_id_counter_;
  
  // Client内存ID计数器
  uint64_t client_id_counter_;

  // 禁用拷贝构造和赋值操作
  HixlMemStore(const HixlMemStore&) = delete;
  HixlMemStore& operator=(const HixlMemStore&) = delete;
};

#endif  // HCCL_MEM_STORE_H_
