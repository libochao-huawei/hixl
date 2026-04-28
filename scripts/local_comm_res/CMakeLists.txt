# CMakeLists.txt for LocalCommRes Test
cmake_minimum_required(VERSION 3.5)
project(local_comm_res_test)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 DCMI 库路径（从环境变量或默认路径）
set(DCMI_SEARCH_PATHS
    /usr/local/Ascend/dcmi/lib
    /usr/local/Ascend/firmware/lib
    /usr/lib64
    /usr/lib
)

find_library(DCMI_LIB dcmi HINTS ${DCMI_SEARCH_PATHS})

# 查找 ACL 库路径
set(ACL_SEARCH_PATHS
    /usr/local/Ascend/aoe/lib
    /usr/local/Ascend/atc/lib64
    /usr/local/Ascend/ascend-toolkit/latest/aoe/lib
    /usr/local/Ascend/ascend-toolkit/latest/atc/lib64
    /usr/lib64
    /usr/lib
)

find_library(ACL_LIB acl HINTS ${ACL_SEARCH_PATHS})

# 头文件路径
set(INCLUDE_PATHS
    ${CMAKE_SOURCE_DIR}
    /usr/local/Ascend/dcmi/include
    /usr/local/Ascend/firmware/include
    /usr/local/Ascend/aoe/include
    /usr/local/Ascend/atc/include
    /usr/local/Ascend/ascend-toolkit/latest/aoe/include
    /usr/local/Ascend/ascend-toolkit/latest/atc/include
    /home/lutianming/CLionProjects/hcomm/src/legacy/framework/topo/topo_addr_info/src
)

include_directories(${INCLUDE_PATHS})

# 源文件
set(SOURCES
    local_comm_res_tool.cc
    test_local_comm_res.cc
)

# 可执行文件
add_executable(test_local_comm_res ${SOURCES})

# 链接库
if(ACL_LIB)
    target_link_libraries(test_local_comm_res ${ACL_LIB})
else()
    target_link_libraries(test_local_comm_res acl)
endif()

if(DCMI_LIB)
    target_link_libraries(test_local_comm_res ${DCMI_LIB})
else()
    target_link_libraries(test_local_comm_res dcmi)
endif()

# 打印配置信息
message(STATUS "========================================")
message(STATUS "LocalCommRes Test Build Configuration")
message(STATUS "========================================")
message(STATUS "ACL_LIB: ${ACL_LIB}")
message(STATUS "DCMI_LIB: ${DCMI_LIB}")
message(STATUS "INCLUDE_PATHS: ${INCLUDE_PATHS}")
message(STATUS "========================================")