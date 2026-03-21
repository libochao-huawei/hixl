#!/bin/bash

# 配置日志等级以及目录
export ASCEND_GLOBAL_LOG_LEVEL=1
export ASCEND_PROCESS_LOG_PATH=/home/hixl/lg/hixl-master/build/benchmarks/log

# 测试程序路径
BENCHMARK_PROGRAM="/home/hixl/lg/hixl-master/build/benchmarks/hixl_cs_benchmark_exception"

echo "========== HIXL CS 异常场景测试工具 =========="
echo "请选择异常测试场景："
echo "5. 场景5 - Server地址解注册后，Client再次传输"
echo "6. 场景6 - 同一连接传输5000次，测试资源上限"
echo "7. 场景7 - 传输过程中销毁Client，测试Server资源回收"
echo "8. 场景8 - 传输过程中Kill掉Client进程，测试Server资源回收"
echo "=============================================="

read -p "请输入选项 (5-8): " test_case

case $test_case in
    5)
        echo "你选择了: 场景5 - Server地址解注册后，Client再次传输"
        ;;
    6)
        echo "你选择了: 场景6 - 同一连接传输5000次"
        ;;
    7)
        echo "你选择了: 场景7 - 传输过程中销毁Client"
        ;;
    8)
        echo "你选择了: 场景8 - 传输过程中Kill掉Client进程"
        ;;
    *)
        echo "错误：无效的选项 '$test_case'，请输入 5-8 的数字"
        exit 1
        ;;
esac

echo ""
echo "请选择传输模式："
echo "1. d2d - Device to Device (设备到设备)"
echo "2. d2h - Device to Host (设备到主机)"
echo "3. h2h - Host to Host (主机到主机)"
echo "4. h2d - Host to Device (主机到设备)"
echo "================================================"

read -p "请输入选项 (1-4): " mode_choice

case $mode_choice in
    1)
        echo "你选择了: d2d (Device to Device)"
        transfer_mode="d2d"
        ;;
    2)
        echo "你选择了: d2h (Device to Host)"
        transfer_mode="d2h"
        ;;
    3)
        echo "你选择了: h2h (Host to Host)"
        transfer_mode="h2h"
        ;;
    4)
        echo "你选择了: h2d (Host to Device)"
        transfer_mode="h2d"
        ;;
    *)
        echo "错误：无效的选项 '$mode_choice'，请输入 1-4 的数字"
        exit 1
        ;;
esac

echo ""
echo "请选择传输操作："
echo "1. write - Client写入Server"
echo "2. read  - Client读取Server"
echo "================================================"

read -p "请输入选项 (1-2): " op_choice

case $op_choice in
    1)
        echo "你选择了: write (Client写入Server)"
        transfer_op="write"
        ;;
    2)
        echo "你选择了: read (Client读取Server)"
        transfer_op="read"
        ;;
    *)
        echo "错误：无效的选项 '$op_choice'，请输入 1-2 的数字"
        exit 1
        ;;
esac

echo ""
echo "================================================"
echo "测试配置："
echo "  异常场景: $test_case"
echo "  传输模式: $transfer_mode"
echo "  传输操作: $transfer_op"
echo "================================================"
echo ""

echo "开始执行异常场景测试..."
echo ""

# 根据传输模式设置通信资源参数
case $transfer_mode in
    d2d)
        local_comm_res='{"location":"device","protocol":"hccs","addr":"192.168.100.100"}'
        remote_comm_res='{"location":"device","protocol":"hccs","addr":"192.168.100.101"}'
        ;;
    d2h)
        local_comm_res='{"location":"device","protocol":"hccs","addr":"192.168.100.100"}'
        remote_comm_res='{"location":"host","protocol":"hccs","addr":"192.168.100.101"}'
        ;;
    h2h)
        local_comm_res='{"location":"host","protocol":"roce","addr":"192.168.100.100"}'
        remote_comm_res='{"location":"host","protocol":"roce","addr":"192.168.100.101"}'
        ;;
    h2d)
        local_comm_res='{"location":"host","protocol":"hccs","addr":"192.168.100.100"}'
        remote_comm_res='{"location":"device","protocol":"hccs","addr":"192.168.100.101"}'
        ;;
esac

# 执行测试程序
# 参数说明:
#   device_id: 设备ID
#   local_engine: 本地引擎地址 (Server模式时使用)
#   remote_engine: 远程引擎地址 (Client模式时使用，格式: IP:PORT)
#   tcp_port: TCP通信端口
#   transfer_mode: 传输模式 (d2d/d2h/h2h/h2d)
#   transfer_op: 传输操作 (write/read)
#   local_comm_res: 本地通信资源配置
#   remote_comm_res: 远程通信资源配置
#   test_case: 异常测试场景 (5-8)

# 启动Server (后台运行)
echo "启动Server端..."
$BENCHMARK_PROGRAM 0 10.23.144.161 10.23.144.163:16000 17000 $transfer_mode $transfer_op "$local_comm_res" "$remote_comm_res" $test_case &
SERVER_PID=$!

# 等待Server启动
sleep 2

# 启动Client
echo "启动Client端..."
$BENCHMARK_PROGRAM 0 10.23.144.161 10.23.144.163:16000 17000 $transfer_mode $transfer_op "$local_comm_res" "$remote_comm_res" $test_case
CLIENT_RET=$?

# 等待Server结束
wait $SERVER_PID
SERVER_RET=$?

echo ""
echo "================================================"
echo "测试执行完成！"
echo "  Client返回值: $CLIENT_RET"
echo "  Server返回值: $SERVER_RET"
echo "================================================"
