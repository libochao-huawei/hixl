#!/bin/bash

# 配置日志等级以及目录
export ASCEND_GLOBAL_LOG_LEVEL=1
export ASCEND_PROCESS_LOG_PATH=/home/hixl/lg/hixl-master/build/benchmarks/log

echo "========== HIXL CS Benchmark 测试工具 =========="
echo "请选择测试场景："
echo "1. d2d - Device to Device (设备到设备)"
echo "2. d2h - Device to Host (设备到主机)"
echo "3. h2h - Host to Host (主机到主机)"
echo "4. h2d - Host to Device (主机到设备)"
echo "================================================"

read -p "请输入选项 (1-4): " choice

case $choice in
    1)
        echo "你选择了: d2d (Device to Device) 测试"
        scenario="d2d"
        ;;
    2)
        echo "你选择了: d2h (Device to Host) 测试"
        scenario="d2h"
        ;;
    3)
        echo "你选择了: h2h (Host to Host) 测试"
        scenario="h2h"
        ;;
    4)
        echo "你选择了: h2d (Host to Device) 测试"
        scenario="h2d"
        ;;
    *)
        echo "错误：无效的选项 '$choice'，请输入 1-4 的数字"
        exit 1
        ;;
esac

echo "开始执行 $scenario 测试用例..."

# 执行测试程序
/home/hixl/lg/hixl-master/build/benchmarks/hixl_cs_benchmark_large 0 10.23.144.161 10.23.144.163:16000 17000 $scenario write 2 "{\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"192.168.100.100\"}" "{\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"192.168.100.101\"}"
echo "测试执行完成！"

