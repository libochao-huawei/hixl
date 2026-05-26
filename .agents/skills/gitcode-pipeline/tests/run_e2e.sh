#!/usr/bin/env bash
# run_e2e.sh - 运行全部或指定 E2E 测试用例
#
# 每个测试用例会: 创建 PR → 触发 CI → 检测失败 → 修复 → 验证通过 → 清理
#
# 用法:
#   ./run_e2e.sh                              # 运行全部测试
#   ./run_e2e.sh compile-error                # 只运行编译错误测试
#   ./run_e2e.sh compile-error ut-error       # 运行指定测试
#   ./run_e2e.sh --list                       # 列出所有测试用例
#   ./run_e2e.sh --skip-cleanup               # 失败时不清理 PR
#
# 环境要求:
#   - GITCODE_API_TOKEN 已设置
#   - git remote hgjupstream 指向 fork 仓库
#   - jq 已安装

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CASES_DIR="${SCRIPT_DIR}/cases"

declare -A TEST_CASES=(
  ["compile-error"]="test_compile_error.sh|编译错误 - 缺少分号 + API retry 验证"
  ["ut-error"]="test_ut_error.sh|UT 执行错误 - 断言值错误导致测试失败"
  ["coredump"]="test_coredump.sh|执行 Coredump - 空指针解引用导致 SIGSEGV"
)

SKIP_CLEANUP=false
TESTS_TO_RUN=()

while [ $# -gt 0 ]; do
  case "$1" in
    --list|-l)
      echo "可用测试用例:"
      echo ""
      for name in $(echo "${!TEST_CASES[@]}" | tr ' ' '\n' | sort); do
        IFS='|' read -r script desc <<< "${TEST_CASES[$name]}"
        printf "  %-15s %s\n" "$name" "$desc"
      done
      exit 0
      ;;
    --skip-cleanup)
      SKIP_CLEANUP=true
      shift
      ;;
    --help|-h)
      echo "用法: run_e2e.sh [选项] [测试名...]"
      echo ""
      echo "选项:"
      echo "  --list          列出所有测试用例"
      echo "  --skip-cleanup  测试失败时不清理 PR 和分支"
      echo "  --help          显示帮助"
      echo ""
      echo "示例:"
      echo "  run_e2e.sh                          # 运行全部"
      echo "  run_e2e.sh compile-error            # 只运行编译错误"
      echo "  run_e2e.sh compile-error ut-error   # 运行多个"
      exit 0
      ;;
    *)
      if [[ -v "TEST_CASES[$1]" ]]; then
        TESTS_TO_RUN+=("$1")
      else
        echo "错误: 未知测试用例 '$1'" >&2
        echo "运行 --list 查看可用用例" >&2
        exit 1
      fi
      shift
      ;;
  esac
done

if [ ${#TESTS_TO_RUN[@]} -eq 0 ]; then
  TESTS_TO_RUN=($(echo "${!TEST_CASES[@]}" | tr ' ' '\n' | sort))
fi

echo "=========================================="
echo " GitCode Pipeline Skill E2E 测试套件"
echo "=========================================="
echo "测试用例: ${TESTS_TO_RUN[*]}"
echo "跳过清理: $SKIP_CLEANUP"
echo "开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

PASSED=()
FAILED=()
SKIPPED=()

for test_name in "${TESTS_TO_RUN[@]}"; do
  IFS='|' read -r script desc <<< "${TEST_CASES[$test_name]}"

  echo "------------------------------------------"
  echo "▶ 运行: $test_name ($desc)"
  echo "------------------------------------------"

  CLEANUP_FLAG=""
  $SKIP_CLEANUP && CLEANUP_FLAG="--skip-cleanup"

  START_TIME=$(date +%s)

  if bash "${CASES_DIR}/${script}" $CLEANUP_FLAG; then
    PASSED+=("$test_name")
    echo "✅ $test_name 通过"
  else
    exit_code=$?
    if [ "$exit_code" -eq 255 ]; then
      SKIPPED+=("$test_name")
      echo "⚠️ $test_name 跳过"
    else
      FAILED+=("$test_name")
      echo "❌ $test_name 失败 (exit code: $exit_code)"
    fi
  fi

  END_TIME=$(date +%s)
  echo "耗时: $((END_TIME - START_TIME))s"
  echo ""
done

echo "=========================================="
echo " 测试报告"
echo "=========================================="
echo "总计: ${#TESTS_TO_RUN[@]}  通过: ${#PASSED[@]}  失败: ${#FAILED[@]}  跳过: ${#SKIPPED[@]}"
echo ""

if [ ${#PASSED[@]} -gt 0 ]; then
  echo "✅ 通过: ${PASSED[*]}"
fi
if [ ${#FAILED[@]} -gt 0 ]; then
  echo "❌ 失败: ${FAILED[*]}"
fi
if [ ${#SKIPPED[@]} -gt 0 ]; then
  echo "⚠️ 跳过: ${SKIPPED[*]}"
fi

echo ""
echo "结束时间: $(date '+%Y-%m-%d %H:%M:%S')"

if [ ${#FAILED[@]} -gt 0 ]; then
  exit 1
fi
