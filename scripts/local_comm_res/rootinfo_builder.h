/**
 * @file rootinfo_builder.h
 * @brief RootInfo 构建模块
 *
 * 提供根据 NPU ID 构建 RootInfo 的功能，包括：
 * - 调用 DCMI 接口获取 URMA 设备信息
 * - 根据产品形态构建 port 到 EID 的映射
 */

#ifndef HIXL_ROOTINFO_BUILDER_H_
#define HIXL_ROOTINFO_BUILDER_H_

#include <string>
#include <vector>
#include <map>

namespace hixl {

// ============ 错误码定义 ============
const int32_t SUCCESS = 0;
const int32_t ERROR_INVALID_PARAM = 1001;
const int32_t ERROR_FILE_NOT_FOUND = 1002;
const int32_t ERROR_FILE_PARSE_FAILED = 1003;
const int32_t ERROR_DCMI_INTERFACE_FAILED = 1004;
const int32_t ERROR_NO_EID_FOUND = 1005;
const int32_t ERROR_NO_CLOS_EID_FOUND = 1006;

// ============ URMA Device 数据结构 ============

/**
 * @brief URMA Device 结构
 * 每个 URMA Device 包含多个 EID，die_id 从第一个 EID 获取
 */
struct UrmaDevice {
    std::string name;                      // 设备名称，如 "udma0"
    std::vector<std::string> eid_list;    // 该设备下的所有 EID 列表
};

// ============ RootInfo 数据结构 ============

/**
 * @brief NPU 串口到 EID 的映射信息
 * key: 串口标识 "die_id/port"
 * value: 对应的 EID 字符串
 */
struct NpuRootInfo {
    std::map<std::string, std::string> port_to_eid;  // Mesh 层串口到 EID 映射
    std::string clos_pg_eid;                          // CLOS 层 PG EID（串口组标识）
};

// ============ EID 解析辅助函数 ============

/**
 * @brief 从 EID 获取 port（单个物理串口）
 * @param [in] eid 字符串格式 EID
 * @return port 编号 (0-9)，port > 9 表示 CLOS 层串口组
 */
int GetPortFromEid(const std::string& eid);

/**
 * @brief 从 EID 获取 die_id（Server 类型）
 * @param [in] eid 字符串格式 EID
 * @return die_id (0 或 1)
 */
int GetServerDieIdFromEid(const std::string& eid);

/**
 * @brief 从 EID 获取 die_id（Pod 类型）
 * @param [in] eid 字符串格式 EID
 * @return die_id (0 或 1)
 */
int GetPodDieIdFromEid(const std::string& eid);

/**
 * @brief 判断 EID 是否为 Mesh 层（单个物理串口）
 * @param [in] eid 字符串格式 EID
 * @return true: Mesh 层 (port ≤ 9), false: CLOS 层 (port > 9)
 */
bool IsMeshLayerEid(const std::string& eid);

/**
 * @brief 判断 EID 是否为 CLOS 层（串口组）
 * @param [in] eid 字符串格式 EID
 * @return true: CLOS 层 (port > 9), false: Mesh 层 (port ≤ 9)
 */
bool IsClosLayerEid(const std::string& eid);

// ============ RootInfo 构建接口 ============

/**
 * @brief 根据 NPU ID 构建 RootInfo
 * @param [in] npu_id NPU ID
 * @param [in] is_server 是否为 Server 产品形态
 * @param [out] rootinfo 输出的 RootInfo 结构
 * @return 成功: SUCCESS, 失败: 其它错误码
 *
 * 该函数内部调用 DCMI 接口获取 URMA 设备信息，
 * 并根据产品形态构建 port 到 EID 的映射。
 *
 * 构建规则（参考 Python 的 process_level0 逻辑）:
 *   - 先获取 urma_device 的 die_id（从第一个 EID）
 *   - 如果 die_id == 0，跳过该 urma_device 下的所有 EID
 *   - 否则遍历其 eid_list 构建 rootinfo
 */
int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo& rootinfo);

/**
 * @brief 获取 URMA Device 列表
 * @param [in] npu_id NPU ID
 * @param [out] urma_devices URMA Device 列表
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice>& urma_devices);

}  // namespace hixl

#endif  // HIXL_ROOTINFO_BUILDER_H_
