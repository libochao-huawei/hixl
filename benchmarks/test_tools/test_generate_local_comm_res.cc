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
 * @file test_generate_local_comm_res.cc
 * @brief GenerateLocalCommRes 接口测试脚本
 *
 * 测试流程：
 * 1. 解析命令行参数（npu_id）
 * 2. 初始化 ACL 运行时
 * 3. 调用 aclrtSetDevice 占用 NPU
 * 4. 调用 aclrtGetDevice 获取当前 logic_id
 * 5. 调用 aclrtGetPhyDevIdByLogicDevId 转换为 phy_id
 * 6. 调用 GenerateLocalCommRes 生成 LocalCommRes
 * 7. 打印结果并清理
 *
 * 编译（假设 build 目录存在）：
 *   cd build && cmake .. && make test_generate_local_comm_res
 *
 * 运行：
 *   ./test_generate_local_comm_res <npu_id>
 *   例如：./test_generate_local_comm_res 0
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include "acl/acl.h"
#include "local_comm_res_generator_v1.h"

namespace {

constexpr int32_t kRetSuccess = 0;
constexpr int32_t kRetFailed = -1;

void PrintUsage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <npu_id>\n"
              << "  npu_id: Logic device ID (0, 1, 2, ...)\n"
              << "Example: " << prog_name << " 0\n";
}

int32_t InitAcl() {
    aclError ret = aclInit(nullptr);
    if (ret != ACL_SUCCESS) {
        std::cerr << "aclInit failed, ret=" << ret << std::endl;
        return kRetFailed;
    }
    std::cout << "ACL initialized successfully\n";
    return kRetSuccess;
}

int32_t FinalizeAcl() {
    aclError ret = aclFinalize();
    if (ret != ACL_SUCCESS) {
        std::cerr << "aclFinalize failed, ret=" << ret << std::endl;
        return kRetFailed;
    }
    std::cout << "ACL finalized successfully\n";
    return kRetSuccess;
}

int32_t SetDevice(int32_t device_id) {
    aclError ret = aclrtSetDevice(device_id);
    if (ret != ACL_SUCCESS) {
        std::cerr << "aclrtSetDevice(" << device_id << ") failed, ret=" << ret << std::endl;
        return kRetFailed;
    }
    std::cout << "aclrtSetDevice(" << device_id << ") succeeded\n";
    return kRetSuccess;
}

int32_t GetCurrentDevice(int32_t* device_id) {
    aclError ret = aclrtGetDevice(device_id);
    if (ret != ACL_SUCCESS) {
        std::cerr << "aclrtGetDevice failed, ret=" << ret << std::endl;
        return kRetFailed;
    }
    std::cout << "Current device: logic_id=" << *device_id << std::endl;
    return kRetSuccess;
}

int32_t GetPhyDevId(int32_t logic_id, int32_t* phy_id) {
    aclError ret = aclrtGetPhyDevIdByLogicDevId(logic_id, phy_id);
    if (ret != ACL_SUCCESS) {
        std::cerr << "aclrtGetPhyDevIdByLogicDevId(" << logic_id << ") failed, ret=" << ret << std::endl;
        return kRetFailed;
    }
    std::cout << "Logic ID " << logic_id << " -> Physical ID " << *phy_id << std::endl;
    return kRetSuccess;
}

void PrintLocalCommRes(const hixl::LocalCommRes& local_comm_res) {
    std::cout << "\n========== LocalCommRes Result ==========\n";
    std::cout << "version: " << local_comm_res.version << "\n";
    std::cout << "net_instance_id: " << local_comm_res.net_instance_id << "\n";
    std::cout << "endpoint_list size: " << local_comm_res.endpoint_list.size() << "\n";

    for (size_t i = 0; i < local_comm_res.endpoint_list.size(); ++i) {
        const auto& ep = local_comm_res.endpoint_list[i];
        std::cout << "  [" << i << "] protocol=" << ep.protocol
                  << ", comm_id=" << ep.comm_id
                  << ", placement=" << ep.placement
                  << ", plane=" << ep.plane
                  << ", dst_eid=" << ep.dst_eid
                  << ", net_instance_id=" << ep.net_instance_id << "\n";
    }
    std::cout << "========================================\n\n";
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        PrintUsage(argv[0]);
        return kRetFailed;
    }

    // 解析 npu_id 参数
    int32_t npu_id = -1;
    try {
        npu_id = std::stoi(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Invalid npu_id: " << argv[1] << std::endl;
        PrintUsage(argv[0]);
        return kRetFailed;
    }

    if (npu_id < 0) {
        std::cerr << "npu_id must be non-negative, got: " << npu_id << std::endl;
        return kRetFailed;
    }

    std::cout << "========================================\n";
    std::cout << "  Test: GenerateLocalCommRes\n";
    std::cout << "  Input npu_id: " << npu_id << "\n";
    std::cout << "========================================\n\n";

    // Step 1: 初始化 ACL
    if (InitAcl() != kRetSuccess) {
        return kRetFailed;
    }

    // Step 2: 占用 NPU
    if (SetDevice(npu_id) != kRetSuccess) {
        FinalizeAcl();
        return kRetFailed;
    }

    // Step 3: 获取当前 logic_id
    int32_t logic_id = -1;
    if (GetCurrentDevice(&logic_id) != kRetSuccess) {
        aclrtResetDevice(npu_id);
        FinalizeAcl();
        return kRetFailed;
    }

    // Step 4: 转换为 physical_id
    int32_t phy_id = -1;
    if (GetPhyDevId(logic_id, &phy_id) != kRetSuccess) {
        aclrtResetDevice(npu_id);
        FinalizeAcl();
        return kRetFailed;
    }

    // Step 5: 调用 GenerateLocalCommRes
    std::cout << "\nCalling GenerateLocalCommRes(phy_id=" << phy_id << ")...\n";
    hixl::LocalCommRes local_comm_res;
    int32_t ret = hixl::GenerateLocalCommRes(phy_id, local_comm_res);
    if (ret != kRetSuccess) {
        std::cerr << "GenerateLocalCommRes failed, ret=" << ret << std::endl;
        aclrtResetDevice(npu_id);
        FinalizeAcl();
        return kRetFailed;
    }

    // Step 6: 打印结果
    PrintLocalCommRes(local_comm_res);

    // Step 7: 清理
    aclrtResetDevice(npu_id);
    FinalizeAcl();

    std::cout << "Test completed successfully!\n";
    return kRetSuccess;
}
