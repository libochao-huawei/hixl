/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef TESTS_DEPENDS_COMMON_HCCN_CONF_UTILS_H_
#define TESTS_DEPENDS_COMMON_HCCN_CONF_UTILS_H_

#include <fstream>
#include <cstdio>
#include <iostream>
#include <string>

namespace test {

inline void WriteHccnConfFile() {
  const std::string file_path = "/tmp/hccn.conf";
  std::ofstream file(file_path);
  if (!file.is_open()) {
    std::cout << "Failed to create file:" << file_path << std::endl;
    return;
  }

  file << "netmask_0=1.2.3.4\n"
       << "address_0=1.1.1.0\n"
       << "netmask_1=1.2.3.4\n"
       << "address_1=1.1.1.1\n"
       << "netmask_2=1.2.3.4\n"
       << "address_2=1.1.1.2\n"
       << "netmask_3=1.2.3.4\n"
       << "address_3=1.1.1.3\n"
       << "netmask_4=1.2.3.4\n"
       << "address_4=1.1.1.4\n"
       << "netmask_5=1.2.3.4\n"
       << "address_5=1.1.1.5\n"
       << "netmask_6=1.2.3.4\n"
       << "address_6=1.1.1.6\n"
       << "netmask_7=1.2.3.4\n"
       << "address_7=1.1.1.7\n";

  file.close();
}

inline void RemoveHccnConfFile() {
  const std::string file_path = "/tmp/hccn.conf";
  if (std::remove(file_path.c_str()) != 0) {
    std::cout << "Failed to delete file:" << file_path.c_str() << std::endl;
  }
}

}  // namespace test

#endif  // TESTS_DEPENDS_COMMON_HCCN_CONF_UTILS_H_
