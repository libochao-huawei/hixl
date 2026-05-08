# CMakeLists.txt for LocalCommRes Test
cmake_minimum_required(VERSION 3.5)
project(local_comm_res_test)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ascend 软件包路径（从环境变量获取）
# 优先使用 ASCEND_HOME_PATH，其次使用 ASCEND_TOOLKIT_HOME
if(DEFINED ENV{ASCEND_HOME_PATH})
    set(ASCEND_ROOT "$ENV{ASCEND_HOME_PATH}")
elseif(DEFINED ENV{ASCEND_TOOLKIT_HOME})
    set(ASCEND_ROOT "$ENV{ASCEND_TOOLKIT_HOME}")
else()
    message(FATAL_ERROR "ASCEND_HOME_PATH or ASCEND_TOOLKIT_HOME environment variable must be set")
endif()

# 根据架构设置库路径
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
    set(ACL_ARCH_DIR "aarch64-linux")
else()
    set(ACL_ARCH_DIR "x86_64-linux")
endif()

# ACL 库和头文件路径
set(ACL_LIB_DIR "${ASCEND_ROOT}/${ACL_ARCH_DIR}/lib64")
set(ACL_INCLUDE_DIR "${ASCEND_ROOT}/${ACL_ARCH_DIR}/include/")

# 头文件路径
set(INCLUDE_PATHS
    ${CMAKE_SOURCE_DIR}
    ${ACL_INCLUDE_DIR}
)

include_directories(${INCLUDE_PATHS})

# 链接库目录
if(EXISTS ${ACL_LIB_DIR})
    link_directories(${ACL_LIB_DIR})
endif()

# 源文件
set(SOURCES
    local_comm_res_tool.cc
    rootinfo_builder.cc
    test_local_comm_res.cc
)

# 可执行文件
add_executable(test_local_comm_res ${SOURCES})

# 链接库
target_link_libraries(test_local_comm_res acl_rt)
target_link_libraries(test_local_comm_res dl)

# 打印配置信息
message(STATUS "========================================")
message(STATUS "LocalCommRes Test Build Configuration")
message(STATUS "========================================")
message(STATUS "ASCEND_ROOT: ${ASCEND_ROOT}")
message(STATUS "ACL_LIB_DIR: ${ACL_LIB_DIR}")
message(STATUS "ACL_INCLUDE_DIR: ${ACL_INCLUDE_DIR}")
message(STATUS "INCLUDE_PATHS: ${INCLUDE_PATHS}")
message(STATUS "========================================")