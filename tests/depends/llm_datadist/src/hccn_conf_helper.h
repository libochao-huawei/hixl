/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_HCCN_CONF_HELPER_H_
#define CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_HCCN_CONF_HELPER_H_

#include <fstream>
#include <cstdio>
#include <iostream>
#include <string>

namespace llm {

// write /tmp/hccn.conf
inline void WriteHccnConfFile() {
  const std::string file_path = "/tmp/hccn.conf";
  std::ofstream file(file_path);
  if (!file.is_open()) {
    std::cout << "Failed to create file:" << file_path << std::endl;
    return;
  }

  file << "netmask_0=255.255.255.0\n"
       << "address_0=192.168.1.0\n"
       << "netmask_1=255.255.255.0\n"
       << "address_1=192.168.1.1\n"
       << "netmask_2=255.255.255.0\n"
       << "address_2=192.168.1.2\n"
       << "netmask_3=255.255.255.0\n"
       << "address_3=192.168.1.3\n"
       << "netmask_4=255.255.255.0\n"
       << "address_4=192.168.1.4\n"
       << "netmask_5=255.255.255.0\n"
       << "address_5=192.168.1.5\n"
       << "netmask_6=255.255.255.0\n"
       << "address_6=192.168.1.6\n"
       << "netmask_7=255.255.255.0\n"
       << "address_7=192.168.1.7\n";

  file.close();
}

// remove /tmp/hccn.conf
inline void RemoveHccnConfFile() {
  const std::string file_path = "/tmp/hccn.conf";
  if (std::remove(file_path.c_str()) != 0) {
    std::cout << "Failed to delete file:" << file_path.c_str() << std::endl;
  }
}

}  // namespace llm

#endif  // CANN_GRAPH_ENGINE_TESTS_DEPENDS_LLM_ENGINE_HCCN_CONF_HELPER_H_
