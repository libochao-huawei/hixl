#ifndef HCCL_ENDPOINT_STORE_H_
#define HCCL_ENDPOINT_STORE_H_

#include <cstdint>
#include <vector>
#include "hccl_types.h"
#include "endpoint.h"

// 前向声明
using EndpointHandle = void *;
using ChannelHandle = void *;

/**
 * @brief 内存描述结构体
 */
struct MemDesc {
  std::string mem_tag;
  HcclMem mem_info;
  MemHandle mem_handle;
};

/**
* @brief EndpointStore管理所有的endpoint功能
*/
class ASCEND_FUNC_VISIBILITY EndpointStore {
 public:
  EndpointStore();
  ~EndpointStore();

  /**
   * @brief 创建端点
   * @param endpoint 端点信息
   * @param endpoint_handle 返回的端点句柄
   * @return 操作结果
   */
  HcclResult CreateEndpoint(const EndPoint &endpoint, EndpointHandle *endpoint_handle);

  /**
   * @brief 销毁端点
   * @param endpoint_handle 端点句柄
   * @return 操作结果
   */
  HcclResult DestroyEndpoint(EndpointHandle endpoint_handle);

  /**
   * @brief 获取所有端点句柄
   * @return 端点句柄列表
   */
  std::vector<EndpointHandle> GetAllEndpointHandles();

  /**
   * @brief 匹配端点
   * @param peer_endpoint 对端端点信息
   * @param endpoint_handle 返回的端点句柄
   * @param matched_endpoint 返回的匹配端点信息
   * @return 操作结果
   */
  HcclResult MatchEndpoint(const EndPoint &peer_endpoint, EndpointHandle *endpoint_handle, EndPoint &matched_endpoint);

  /**
   * @brief 注册内存
   * @param endpoint_handle 端点句柄
   * @param mem_tag 内存标签
   * @param mem 内存信息
   * @param mem_handle 返回的内存句柄
   * @return 操作结果
   */
  HcclResult RegisterMem(EndpointHandle endpoint_handle, const char *mem_tag, const HcclMem *mem, MemHandle *mem_handle);

  /**
   * @brief 注销内存
   * @param endpoint_handle 端点句柄
   * @param mem_handle 内存句柄
   * @return 操作结果
   */
  HcclResult DeregisterMem(EndpointHandle endpoint_handle, MemHandle mem_handle);

  /**
   * @brief 导出内存描述信息
   * @param endpoint_handle 端点句柄
   * @param mem_descs 返回的内存描述列表
   * @return 操作结果
   */
  HcclResult ExportMem(EndpointHandle endpoint_handle, std::vector<MemDesc> &mem_descs);

  /**
   * @brief 创建通道
   * @param endpoint_handle 端点句柄
   * @param channel_handle 返回的通道句柄
   * @return 操作结果
   */
  HcclResult CreateChannel(EndpointHandle endpoint_handle, ChannelHandle *channel_handle);

  /**
   * @brief 销毁通道
   * @param endpoint_handle 端点句柄
   * @param channel_handle 通道句柄
   * @return 操作结果
   */
  HcclResult DestroyChannel(EndpointHandle endpoint_handle, ChannelHandle channel_handle);

 private:
  // 端点信息映射表
  std::unordered_map<EndpointHandle, EndPoint> endpoints_;
  std::mutex endpoints_mutex_;
  
  // 禁用拷贝构造和赋值操作
  EndpointStore(const EndpointStore&) = delete;
  EndpointStore& operator=(const EndpointStore&) = delete;
};

#endif  // HCCL_ENDPOINT_STORE_H_
