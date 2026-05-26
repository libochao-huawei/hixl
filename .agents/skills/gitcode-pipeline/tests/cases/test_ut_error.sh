#!/usr/bin/env bash
# test_ut_error.sh - E2E: UT 执行失败 → 检测 → 修复 → 通过
#
# 测试流程:
#   1. 在 math_util_unittest.cc 中修改一个断言使其必定失败
#   2. 提交 PR，触发 CI
#   3. 等待 CI 失败
#   4. 用 skill 脚本分析: 获取子流水线 → 获取日志 → 定位 FAILED test case
#   5. 修复断言，推送
#   6. 等待 CI 通过
#   7. 清理

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

SKIP_CLEANUP=false
[[ "${1:-}" == "--skip-cleanup" ]] && SKIP_CLEANUP=true

TARGET_FILE="tests/graph_metadef/ut/graph/testcase/math_util_unittest.cc"
TEST_NAME="ut-error"
BRANCH_NAME=""
PR_NUMBER=""

cleanup() {
  if $SKIP_CLEANUP; then
    log_warn "跳过清理 (PR #${PR_NUMBER:-?}, 分支: ${BRANCH_NAME:-?})"
    return
  fi
  log_step "清理..."
  [ -n "${PR_NUMBER:-}" ] && delete_pr "$PR_NUMBER"
  [ -n "${BRANCH_NAME:-}" ] && delete_test_branch "$BRANCH_NAME"
  restore_original_branch
}
trap cleanup EXIT

save_original_branch

# ============================================================
# Phase 1: 引入 UT 断言失败
# ============================================================
log_step "Phase 1: 创建分支并引入 UT 断言错误"

BRANCH_NAME=$(create_test_branch "$TEST_NAME")

log_info "在 $TARGET_FILE 中修改断言 EXPECT_EQ(ret, 300) -> EXPECT_EQ(ret, 999)"
sed -i 's/EXPECT_EQ(ret, 300)/EXPECT_EQ(ret, 999)/' "$TARGET_FILE"

git add "$TARGET_FILE"
git commit -m "test(e2e): 引入 UT 断言失败 - EXPECT_EQ(ret, 999)"

push_to_fork "$BRANCH_NAME"

# ============================================================
# Phase 2: 创建 PR 并触发 CI
# ============================================================
log_step "Phase 2: 创建 PR 并触发 CI"

PR_NUMBER=$(create_pr "$BRANCH_NAME" "[E2E-TEST] UT 执行错误检测" "E2E 测试 - UT 断言失败场景")
trigger_pipeline "$PR_NUMBER"

# ============================================================
# Phase 3: 等待 CI 失败
# ============================================================
log_step "Phase 3: 等待 CI 失败"

PIPELINE_INFO=$(wait_pipeline_status "$PR_NUMBER" "failed|canceled")
PIPELINE_STATUS=$(echo "$PIPELINE_INFO" | grep -oP 'status=\K[^\s]+')
assert_eq "failed" "$PIPELINE_STATUS" "期望流水线失败"

# ============================================================
# Phase 4: 分析失败 - 按 skill 步骤 6.1 严格流程
# ============================================================
log_step "Phase 4: 分析 UT 失败（严格按 SKILL.md 步骤 6.1）"

PID=$(echo "$PIPELINE_INFO" | grep -oP 'pipeline_id=\K[^\s]+')
PRID=$(echo "$PIPELINE_INFO" | grep -oP 'pipeline_run_id=\K[^\s]+')
DETAIL=$(echo "$PIPELINE_INFO" | sed 's/.*pipeline_detail=//')

# 步骤 5.1: 获取主流水线详情
log_info "步骤 5.1: 获取主流水线详情"
DETAIL_OUTPUT=$(bash "${GP_SCRIPTS_DIR}/gp-detail.sh" "$PID" "$PRID" "$DETAIL")
assert_contains "$DETAIL_OUTPUT" "FAILED" "主流水线应为 FAILED"

# 找到 llt 相关失败 Job
FAILED_JOBS=$(echo "$DETAIL_OUTPUT" | grep "FAILED" | grep -iE "llt|ut|test")
assert_contains "$FAILED_JOBS" "FAILED" "应有 UT/LLT 相关 Job 失败"

# 穿透子流水线
STEP_ID=$(echo "$FAILED_JOBS" | grep -oP 'step_id=\K[^\s]+' | head -1)
if [ -n "$STEP_ID" ]; then
  log_info "步骤 5.2: 穿透子流水线"
  SUB_OUTPUT=$(bash "${GP_SCRIPTS_DIR}/gp-sub-output.sh" "$PID" "$PRID" "$DETAIL" "$STEP_ID")
  SUB_PID=$(echo "$SUB_OUTPUT" | grep -oP 'sub_pipeline_id=\K[^\s]+')
  SUB_PRID=$(echo "$SUB_OUTPUT" | grep -oP 'sub_pipeline_run_id=\K[^\s]+')

  SUB_DETAIL_OUTPUT=$(bash "${GP_SCRIPTS_DIR}/gp-detail.sh" "$SUB_PID" "$SUB_PRID" "$DETAIL")
  log_info "子流水线详情:\n$SUB_DETAIL_OUTPUT"

  JOB_ID=$(echo "$SUB_DETAIL_OUTPUT" | grep "FAILED" | grep -oP 'id=\K[^\s]+' | head -1)
  LOG_PID="$SUB_PID"
  LOG_PRID="$SUB_PRID"
else
  JOB_ID=$(echo "$FAILED_JOBS" | grep -oP 'id=\K[^\s]+' | head -1)
  LOG_PID="$PID"
  LOG_PRID="$PRID"
fi

# 步骤 6.1 第1步: 从尾部日志提取 CTest 汇总
log_info "步骤 6.1 第1步: 获取尾部日志 CTest 汇总"
TAIL_LOG=$(bash "${GP_SCRIPTS_DIR}/gp-log.sh" "$LOG_PID" "$LOG_PRID" "$JOB_ID" "$DETAIL" 50)
log_info "尾部日志:\n$TAIL_LOG"

# 验证 CTest 失败特征
assert_contains "$TAIL_LOG" "tests passed|tests failed|FAILED|fail" "日志应包含测试失败信息"

# 从尾部日志中直接提取 FAILED test case（避免全量日志翻页超时）
FAILED_TESTS=$(echo "$TAIL_LOG" | grep '\[  FAILED  \]' || true)
if [ -n "$FAILED_TESTS" ]; then
  log_info "尾部日志中的 FAILED test cases:\n$FAILED_TESTS"
  assert_contains "$FAILED_TESTS" "MathUtilUT" "应定位到 MathUtilUT 相关测试"
fi

# 非 QUICK_MODE 时获取全量日志定位更多细节
if ! $QUICK_MODE; then
  log_info "步骤 6.1 第2步: 获取全量日志定位具体失败用例"
  bash "${GP_SCRIPTS_DIR}/gp-log-full.sh" "$LOG_PID" "$LOG_PRID" "$JOB_ID" "$DETAIL" 2>&1 | tail -3
  FULL_LOG_FILE="pipeline_logs/${JOB_ID}_full.log"

  if [ -f "$FULL_LOG_FILE" ]; then
    FAILED_TESTS=$(grep '\[  FAILED  \]' "$FULL_LOG_FILE" || true)
    log_info "具体失败的 test cases:\n$FAILED_TESTS"
    assert_contains "$(cat "$FULL_LOG_FILE")" "MathUtilUT|math_util" "应定位到 math_util 相关测试"
    EXPECTED_ACTUAL=$(grep -A 2 "Expected.*999|Value of: ret" "$FULL_LOG_FILE" | head -10 || true)
    log_info "断言详情:\n$EXPECTED_ACTUAL"
  else
    log_warn "全量日志文件未找到: $FULL_LOG_FILE，使用尾部日志分析"
  fi
fi

log_info "✅ Phase 4 通过: 成功检测到 UT 执行失败并定位到具体用例"

if $QUICK_MODE; then
  log_info "✅ QUICK_MODE: 跳过修复→通过循环，skill 检测能力已验证"
  log_info "=========================================="
  log_info "✅ E2E 测试 [UT 执行错误] 检测阶段通过 (QUICK_MODE)!"
  log_info "=========================================="
  exit 0
fi

# ============================================================
# Phase 5: 修复断言
# ============================================================
log_step "Phase 5: 修复 UT 断言"

sed -i 's/EXPECT_EQ(ret, 999)/EXPECT_EQ(ret, 300)/' "$TARGET_FILE"
git add "$TARGET_FILE"
git commit -m "fix(e2e): 修复 UT 断言 - 恢复 EXPECT_EQ(ret, 300)"
push_to_fork "$BRANCH_NAME"

sleep 10
trigger_pipeline "$PR_NUMBER"

# ============================================================
# Phase 6: 等待 CI 通过
# ============================================================
log_step "Phase 6: 等待 CI 通过"

FINAL_INFO=$(wait_pipeline_status "$PR_NUMBER" "success|failed|canceled")
FINAL_STATUS=$(echo "$FINAL_INFO" | grep -oP 'status=\K[^\s]+')

if [ "$FINAL_STATUS" != "success" ]; then
  log_error "CI 修复后未通过，状态: $FINAL_STATUS"
  bash "${GP_SCRIPTS_DIR}/gp-analyze-failure.sh" "$PR_NUMBER" || true
  if $SKIP_CLEANUP; then
    log_warn "PR #${PR_NUMBER} 保留，请手动排查"
    exit 1
  fi
fi

assert_eq "success" "$FINAL_STATUS" "修复后 CI 应通过"

sleep 30
check_pr_label "$PR_NUMBER" "ci-pipeline-passed" && log_info "✅ ci-pipeline-passed label 已添加" \
  || log_warn "ci-pipeline-passed label 未检测到"

log_info "=========================================="
log_info "✅ E2E 测试 [UT 执行错误] 全部通过!"
log_info "=========================================="
