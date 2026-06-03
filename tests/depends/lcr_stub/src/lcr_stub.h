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
 * @file lcr_stub.h
 * @brief UT 专用测试桩注入接口
 *
 * 提供 GenerateLocalCommRes 的桩函数注入机制，使 hixl::GenerateLocalCommResJson
 * 的 SUCCESS 路径（JSON 序列化）在 UT 环境下可被覆盖。**仅供单元测试使用**，
 * 非线程安全。
 */

#ifndef HIXL_TESTS_DEPENDS_LCR_STUB_SRC_LCR_STUB_H
#define HIXL_TESTS_DEPENDS_LCR_STUB_SRC_LCR_STUB_H

#include "local_comm_res_generator_v1.h"

namespace hixl {
namespace test_stub {

/**
 * @brief 注入 GenerateLocalCommRes 的桩实现
 *
 * 注入后，hixl::GenerateLocalCommRes(phy_dev_id, local_comm_res) 会优先调用
 * stub 函数而不是真实实现。**仅在 UT 场景下使用**。
 *
 * @param [in] stub 桩函数指针，签名为：
 *                  int32_t (*)(int32_t phy_dev_id, LocalCommRes &local_comm_res)
 *                  传 nullptr 等价于 ClearGenerateLocalCommResStub()。
 */
void SetGenerateLocalCommResStub(int32_t (*stub)(int32_t, LocalCommRes &));

/**
 * @brief 清除 GenerateLocalCommRes 的桩，恢复真实实现
 */
void ClearGenerateLocalCommResStub();

}  // namespace test_stub
}  // namespace hixl

#endif  // HIXL_TESTS_DEPENDS_LCR_STUB_SRC_LCR_STUB_H
