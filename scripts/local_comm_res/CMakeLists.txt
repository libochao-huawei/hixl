# CMakeLists.txt for LocalCommRes Test
cmake_minimum_required(VERSION 3.5)
project(local_comm_res_test)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 ACL 库路径
set(ACL_SEARCH_PATHS
    /usr/local/Ascend/aoe/lib64
    /usr/local/Ascend/atc/lib64
    /usr/local/Ascend/ascend-toolkit/latest/aoe/lib64
    /usr/local/Ascend/ascend-toolkit/latest/atc/lib64
    /usr/lib64
    /usr/lib
)

find_library(ACL_LIB acl HINTS ${ACL_SEARCH_PATHS})

# 头文件路径（不需要 hal.h 了）
set(INCLUDE_PATHS
    ${CMAKE_SOURCE_DIR}
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

# 动态链接 dl（用于 dlopen）
target_link_libraries(test_local_comm_res dl)

# 打印配置信息
message(STATUS "========================================")
message(STATUS "LocalCommRes Test Build Configuration")
message(STATUS "========================================")
message(STATUS "ACL_LIB: ${ACL_LIB}")
message(STATUS "INCLUDE_PATHS: ${INCLUDE_PATHS}")
message(STATUS "========================================")