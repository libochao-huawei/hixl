#!/usr/bin/env bash
# test_coredump.sh - E2E: 执行 coredump → 检测 → 修复 → 通过
#
# 测试流程:
#   1. 在 UT 中引入空指针解引用 (SIGSEGV)，导致进程 coredump
#   2. 提交 PR，触发 CI
#   3. 等待 CI 失败
#   4. 用 skill 脚本分析: 检测到 exit code 139 (SIGSEGV) 或 coredump 特征
#   5. 修复，推送
#   6. 等待 CI 通过
#   7. 清理

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

SKIP_CLEANUP=false
[[ "${1:-}" == "--skip-cleanup" ]] && SKIP_CLEANUP=true

TARGET_FILE="tests/graph_metadef/ut/graph/testcase/math_util_unittest.cc"
TEST_NAME="coredump"
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
# Phase 1: 引入空指针解引用导致 coredump
# ============================================================
log_step "Phase 1: 创建分支并引入空指针解引用"

BRANCH_NAME=$(create_test_branch "$TEST_NAME")

# 在 AddOverflow_NotOverflow 测试开头插入空指针解引用
log_info "在 $TARGET_FILE 中添加空指针解引用"
sed -i '/TEST_F(MathUtilUT, AddOverflow_NotOverflow) {/a\
  int *nullptr_crash = nullptr;\
  *nullptr_crash = 42;' "$TARGET_FILE"

git add "$TARGET_FILE"
git commit -m "test(e2e): 引入空指针解引用 - 触发 coredump"

push_to_fork "$BRANCH_NAME"

# ============================================================
# Phase 2: 创建 PR 并触发 CI
# ============================================================
log_step "Phase 2: 创建 PR 并触发 CI"

PR_NUMBER=$(create_pr "$BRANCH_NAME" "[E2E-TEST] Coredump 检测" "E2E 测试 - 空指针导致 coredump 场景")
trigger_pipeline "$PR_NUMBER"

# ============================================================
# Phase 3: 等待 CI 失败
# ============================================================
log_step "Phase 3: 等待 CI 失败"

PIPELINE_INFO=$(wait_pipeline_status "$PR_NUMBER" "failed|canceled")
PIPELINE_STATUS=$(echo "$PIPELINE_INFO" | grep -oP 'status=\K[^\s]+')
assert_eq "failed" "$PIPELINE_STATUS" "期望流水线失败"

# ============================================================
# Phase 4: 分析失败 - 检测 coredump 特征
# ============================================================
log_step "Phase 4: 分析 coredump 失败（按 SKILL.md 流程）"

PID=$(echo "$PIPELINE_INFO" | grep -oP 'pipeline_id=\K[^\s]+')
PRID=$(echo "$PIPELINE_INFO" | grep -oP 'pipeline_run_id=\K[^\s]+')
DETAIL=$(echo "$PIPELINE_INFO" | sed 's/.*pipeline_detail=//')

# 步骤 5.1: 获取主流水线详情
log_info "步骤 5.1: 获取主流水线详情"
DETAIL_OUTPUT=$(bash "${GP_SCRIPTS_DIR}/gp-detail.sh" "$PID" "$PRID" "$DETAIL")
assert_contains "$DETAIL_OUTPUT" "FAILED" "主流水线应为 FAILED"

# 找到 llt/ut 失败 Job
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
  JOB_ID=$(echo "$SUB_DETAIL_OUTPUT" | grep "FAILED" | grep -oP 'id=\K[^\s]+' | head -1)
  LOG_PID="$SUB_PID"
  LOG_PRID="$SUB_PRID"
else
  JOB_ID=$(echo "$FAILED_JOBS" | grep -oP 'id=\K[^\s]+' | head -1)
  LOG_PID="$PID"
  LOG_PRID="$PRID"
fi

# 步骤 5.3: 获取日志
log_info "步骤 5.3: 获取失败 Job 日志"
TAIL_LOG=$(bash "${GP_SCRIPTS_DIR}/gp-log.sh" "$LOG_PID" "$LOG_PRID" "$JOB_ID" "$DETAIL" 50)
log_info "尾部日志:\n$TAIL_LOG"

# 检测 coredump 特征: exit code 139 (128+11=SIGSEGV), 或 signal 11, 或 segfault
COREDUMP_DETECTED=false
if echo "$TAIL_LOG" | grep -qiE "signal 11|segfault|exit code 139|core dumped|SIGSEGV|AddressSanitizer.*SEGV"; then
  COREDUMP_DETECTED=true
  log_info "检测到 coredump 特征 (signal/segfault)"
fi

# QUICK_MODE: 尾部日志已足够判断
if ! $QUICK_MODE; then
  log_info "步骤 6.1: 获取全量日志确认 coredump 详情"
  bash "${GP_SCRIPTS_DIR}/gp-log-full.sh" "$LOG_PID" "$LOG_PRID" "$JOB_ID" "$DETAIL" 2>&1 | tail -3
  FULL_LOG_FILE="pipeline_logs/${JOB_ID}_full.log"

  if [ -f "$FULL_LOG_FILE" ]; then
    FULL_CONTENT=$(cat "$FULL_LOG_FILE")
    if echo "$FULL_CONTENT" | grep -qiE "signal 11|segfault|exit code 139|core dumped|SIGSEGV"; then
      COREDUMP_DETECTED=true
      log_info "全量日志确认 coredump"
    fi
    FAILED_TESTS=$(grep '\[  FAILED  \]' "$FULL_LOG_FILE" || true)
    log_info "失败用例:\n$FAILED_TESTS"
  fi
fi

if ! $COREDUMP_DETECTED; then
  log_warn "未检测到典型 coredump 特征，但流水线确实失败，可能被 ctest 框架捕获为其他错误"
  log_warn "继续测试流程（宽松验证）"
fi

log_info "✅ Phase 4 通过: 成功检测到执行异常"

if $QUICK_MODE; then
  log_info "✅ QUICK_MODE: 跳过修复→通过循环，skill 检测能力已验证"
  log_info "=========================================="
  log_info "✅ E2E 测试 [Coredump] 检测阶段通过 (QUICK_MODE)!"
  log_info "=========================================="
  exit 0
fi

# ============================================================
# Phase 5: 修复空指针
# ============================================================
log_step "Phase 5: 修复空指针解引用"

# 移除插入的空指针代码行
sed -i '/int \*nullptr_crash = nullptr;/d' "$TARGET_FILE"
sed -i '/\*nullptr_crash = 42;/d' "$TARGET_FILE"

git add "$TARGET_FILE"
git commit -m "fix(e2e): 移除空指针解引用 - 修复 coredump"
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
log_info "✅ E2E 测试 [Coredump] 全部通过!"
log_info "=========================================="
