
#!/bin/bash
# build_and_test.sh - 构建并运行 LocalCommRes 测试用例

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUTPUT_FILE="${SCRIPT_DIR}/test_output.json"

echo "========================================"
echo "  LocalCommRes Test Build Script"
echo "========================================"

# 创建构建目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 检查 Ascend 工具包路径
ASCEND_PATHS=(
    "/usr/local/Ascend"
    "/home/lutianming/Ascend"
)

ASCEND_ROOT=""
for path in "${ASCEND_PATHS[@]}"; do
    if [ -d "$path" ]; then
        ASCEND_ROOT="$path"
        break
    fi
done

if [ -z "$ASCEND_ROOT" ]; then
    echo "WARNING: Ascend toolkit not found in standard paths"
else
    echo "Found Ascend at: $ASCEND_ROOT"
fi

# CMake 配置
echo ""
echo "[1/3] CMake configuration..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DASCEND_ROOT="${ASCEND_ROOT}" \
    2>&1

# 编译
echo ""
echo "[2/3] Building..."
make -j$(nproc) 2>&1

# 检查可执行文件
if [ ! -f "${BUILD_DIR}/test_local_comm_res" ]; then
    echo "ERROR: Build failed, test_local_comm_res not found"
    exit 1
fi

echo ""
echo "[3/3] Running test..."
echo ""

# 运行测试（可以使用 --device 指定设备 ID）
cd "${SCRIPT_DIR}"
"${BUILD_DIR}/test_local_comm_res" "$@"

# 显示输出文件
if [ -f "${OUTPUT_FILE}" ]; then
    echo ""
    echo "========================================"
    echo "  Output file: ${OUTPUT_FILE}"
    echo "========================================"
fi

echo ""
echo "Done!"