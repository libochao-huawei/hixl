/**
* Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file rootinfo_builder.h
 * @brief RootInfo 构建模块
 *
 * 提供根据 NPU ID 构建 RootInfo 的功能，包括：
 * - 调用 DCMI 接口获取 URMA 设备信息
 * - 根据产品形态构建 port 到 EID 的映射
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H

#include <string>
#include <vector>
#include <map>
#include "hixl/hixl_types.h"

namespace hixl {

// ============ DCMI 相关数据结构 ============

const int32_t DCMI_URMA_EID_SIZE = 16;
const int32_t MAX_EID_PER_UE = 32;

/**
 * @brief DCMI URMA EID 结构
 */
typedef union dcmi_urma_eid {
    unsigned char raw[DCMI_URMA_EID_SIZE];
    struct {
        unsigned long subnet_prefix;
        unsigned long interface_id;
    } in6;
} dcmi_urma_eid_t;

/**
 * @brief DCMI URMA EID 信息结构
 */
typedef struct dcmi_urma_eid_info {
    dcmi_urma_eid_t eid;
    unsigned int eid_index;
} dcmi_urma_eid_info_t;

// ============ 错误码定义 ============
// SUCCESS 已通过 hixl_types.h 定义
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
 * @brief CLOS PG EID 信息
 */
struct ClosPgEidInfo {
    std::string eid;       // CLOS PG EID
    int die_id;           // 该 PG EID 对应的 die_id
};

/**
 * @brief NPU 串口到 EID 的映射信息
 * key: 串口标识 "die_id/port"
 * value: 对应的 EID 字符串
 */
struct NpuRootInfo {
    std::map<std::string, std::string> port_to_eid;  // Mesh 层串口到 EID 映射
    std::vector<ClosPgEidInfo> clos_pg_eids;          // CLOS 层 PG EID 列表（可能有多个）
};

/**
 * @brief EID 第6字节解析结果
 */
struct EidByte6Info {
    int byte6;           // 原始第6字节值
    int high_nibble;    // 高4位
    int low_nibble;     // 低4位
    int die_id;         // die_id (0 或 1)
    bool is_pg_eid;     // 是否为 PG EID (串口组)
    int port;           // port 值 (0-15)
};

// ============ EID 解析辅助函数 ============

/**
 * @brief 解析 EID 的第6字节
 * @param [in] eid 字符串格式 EID
 * @return EidByte6Info 解析结果
 */
EidByte6Info ParseEidByte6(const std::string& eid);

// ============ RootInfo 构建接口 ============

/**
 * @brief 根据 NPU ID 构建 RootInfo
 * @param [in] npu_id NPU ID
 * @param [in] is_server 是否为 Server 产品形态
 * @param [out] rootinfo 输出的 RootInfo 结构
 * @param [in] json_path 如果不为空，则从指定 JSON 文件加载 EID 列表
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
int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo& rootinfo,
                         const std::string& json_path = "");

/**
 * @brief 获取 URMA Device 列表
 * @param [in] npu_id NPU ID
 * @param [out] urma_devices URMA Device 列表
 * @param [in] json_path 如果不为空，则从指定 JSON 文件加载（跳过 DCMI 调用）
 * @return 成功: SUCCESS, 失败: 其它错误码
 */
int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice>& urma_devices,
                          const std::string& json_path = "");

/**
 * @brief 确定 Mesh 层的 die_id
 * @param npu_id NPU ID
 * @param is_server 是否为 Server 产品形态
 * @return Mesh 层所在的 die_id
 *
 * Server: Mesh 在 1die
 * Pod: 根据 npu_id % 8 判断，0-3 在 0die，4-7 在 1die
 */
int GetMeshDieId(int32_t npu_id, bool is_server);

// ============ DCMI 加载接口（共享） ============

typedef int (*dcmi_init_func)();
typedef int (*dcmi_get_urma_device_cnt_func)(int npu_id, unsigned int* dev_cnt);
typedef int (*dcmi_get_eid_list_func)(int npu_id, int urma_dev_index,
                                       dcmi_urma_eid_info_t* eid_list, int* eid_cnt);
typedef int (*dcmi_get_mainboard_id_func)(int npu_id, unsigned int* mainboard_id);
typedef int (*dcmi_get_logicid_from_phyid_func)(unsigned int phy_id, unsigned int* logic_id);
typedef int (*dcmi_get_device_info_func)(int npu_id, int main_cmd,
                                          unsigned int sub_cmd, void* buf, unsigned int* size);

extern dcmi_init_func g_dcmi_init;
extern dcmi_get_urma_device_cnt_func g_dcmi_get_urma_device_cnt;
extern dcmi_get_eid_list_func g_dcmi_get_eid_list;
extern dcmi_get_mainboard_id_func g_dcmi_get_mainboard_id;
extern dcmi_get_logicid_from_phyid_func g_dcmi_get_logicid_from_phyid;
extern dcmi_get_device_info_func g_dcmi_get_device_info;

int LoadDcmi();

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H
