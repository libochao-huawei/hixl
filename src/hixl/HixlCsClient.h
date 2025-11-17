#ifndef HCCL_Hixl_CS_CLIENT_H_
#define HCCL_Hixl_CS_CLIENT_H_

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include "hccl_types.h"
#include "endpoint.h"

/**
 * @brief Client端单边通信管理类
 * 
 * 负责Client端的内存注册/注销、批量读写操作和状态查询
 */
class ASCEND_FUNC_VISIBILITY HixlCsClient {
 public:
  HixlCsClient();
  ~HixlCsClient();

  /**
   * @brief 创建客户端
   * @param server_ip 服务端IP地址
   * @param server_port 服务端端口号
   * @param src_end_point 源端点信息
   * @param dst_end_point 目标端点信息
   * @param client_handle 输出的客户端句柄
   * @return 操作结果
   */
  HcclResult Create(char* server_ip, uint32_t server_port, 
                    const EndPoint* src_end_point, const EndPoint* dst_end_point, 
                    ClientHandle* client_handle);

  /**
   * @brief 连接到服务端
   * @param client_handle 客户端句柄
   * @return 操作结果
   */
  HcclResult Connect(ClientHandle* client_handle);

  /**
   * @brief 查询客户端的创建完成状态
   * @param client_handle 客户端句柄
   * @param status 返回的状态
   * @return 操作结果
   */
  HcclResult GetStatus(ClientHandle* client_handle, int32_t* status);

  /**
   * @brief 注册client端的endpoint信息到内存中
   * @param client_handle 客户端句柄
   * @param mem_tag 内存标签标识
   * @param mem 内存信息
   * @param mem_handle 返回的内存句柄
   * @return 操作结果
   */
  HcclResult ClientRegMem(ClientHandle* client_handle, const char *mem_tag, const HcclMem *mem, MemHandle *mem_handle);

  /**
   * @brief 获取远端内存信息
   * @param client_handle 客户端句柄
   * @param remote_mem_list 返回的远端内存列表
   * @param mem_tag_list 返回的内存标签列表
   * @param list_num 返回的列表数量
   * @param timeout 超时时间
   * @return 操作结果
   */
  HcclResult GetRemoteMem(ClientHandle* client_handle, HcclMem** remote_mem_list, 
                          char** mem_tag_list, uint32_t* list_num, uint32_t timeout);

  /**
   * @brief 注销内存中的client信息
   * @param client_handle 客户端句柄
   * @param mem_handle 内存句柄
   * @return 操作结果
   */
  HcclResult ClientUnregMem(ClientHandle* client_handle, MemHandle* mem_handle);

  /**
   * @brief 批量写入数据到Server端
   * @param client_handle 客户端句柄
   * @param list_num 地址列表数量
   * @param remote_buf_list 远端内存地址列表
   * @param local_buf_list 本地内存地址列表
   * @param len_list 内存大小列表
   * @param complete_handle 返回的完成句柄
   * @return 操作结果
   */
  HcclResult BatchPut(void* client_handle, uint32_t list_num, void** remote_buf_list, 
                          const void** local_buf_list, uint64_t* len_list, void** complete_handle);

  /**
   * @brief 批量从Server端读取数据
   * @param client_handle 客户端句柄
   * @param list_num 地址列表数量
   * @param remote_buf_list 远端内存地址列表
   * @param local_buf_list 本地内存地址列表
   * @param len_list 内存大小列表
   * @param complete_handle 返回的完成句柄
   * @return 操作结果
   */
  HcclResult BatchGet(void* client_handle, uint32_t list_num, void** remote_buf_list, 
                          const void** local_buf_list, uint64_t* len_list, void** complete_handle);

  /**
   * @brief 查询批量操作的完成状态
   * @param client_handle 客户端句柄
   * @param complete_handle 完成句柄
   * @param status 返回的状态
   * @return 操作结果
   */
  HcclResult QueryCompleteStatus(void* client_handle, void* complete_handle, int32_t* status);

  /**
   * @brief 销毁客户端
   * @param client_handle 客户端句柄
   * @return 操作结果
   */
  HcclResult Destroy(ClientHandle* client_handle);

 private:
  // 内存区域信息结构体
  struct EndpointMemInfo {
    EndpointHandle endpoint_handle;
    MemHandle mem_handle;
    std::string mem_tag;
  };

  // 通道信息结构体
  struct EndpointChannelInfo {
    ChannelHandle channel_handle;
    EndpointHandle endpoint_handle;
    int32_t fd;  // 客户端连接的fd
  };

  std::string client_ip_;  // 客户端IP地址
  uint32_t client_port_;   // 客户端端口号
  EndPoint src_end_point_; // 源端点信息
  EndPoint dst_end_point_; // 目标端点信息
  bool is_connected_;      // 连接状态标志

  // 保存远端的注册内存信息：MemHandle -> 内存信息列表
  std::map<MemHandle, std::vector<EndpointMemInfo>> remote_mem_info_;

  // 保存与服务端的channel信息：fd -> 通道信息
  std::map<int32_t, EndpointChannelInfo> channels_;

  // 完成句柄映射表：complete_handle -> 操作状态
  std::map<void*, int32_t> complete_handles_;

  std::mutex mutex_;       // 主要互斥锁
  std::mutex chn_mutex_;   // 通道互斥锁
  std::mutex complete_mutex_; // 完成状态互斥锁

  // 禁用拷贝构造和赋值操作
  HixlCsClient(const HixlCsClient&) = delete;
  HixlCsClient& operator=(const HixlCsClient&) = delete;
};

#endif  // HCCL_Hixl_CS_CLIENT_H_
