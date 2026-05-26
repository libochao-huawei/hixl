#!/usr/bin/env bash
# test_compile_error.sh - E2E: 编译错误 → 检测 → 修复 → 通过
#
# 测试流程:
#   1. 在 fmk_error_codes.cc 中引入缺少分号的语法错误
#   2. 提交 PR，触发 CI
#   3. 轮询等待 CI 失败
#   4. 用 skill 脚本分析失败，验证检测到编译错误
#   5. 修复错误，推送
#   6. 轮询等待 CI 通过
#   7. 清理 PR 和分支
#
# 用法: ./test_compile_error.sh [--skip-cleanup]
#   --skip-cleanup  测试失败时不清理 PR，便于手动排查

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

SKIP_CLEANUP=false
[[ "${1:-}" == "--skip-cleanup" ]] && SKIP_CLEANUP=true

TARGET_FILE="base/common/fmk_error_codes.cc"
TEST_NAME="compile-error"
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
# Phase 1: 创建分支并引入编译错误
# ============================================================
log_step "Phase 1: 创建分支并引入编译错误"

BRANCH_NAME=$(create_test_branch "$TEST_NAME")

log_info "在 $TARGET_FILE 中引入缺少分号的语法错误"
sed -i 's/DEF_ERRORNO(SUCCESS, "Success");/DEF_ERRORNO(SUCCESS, "Success")/' "$TARGET_FILE"

git add "$TARGET_FILE"
git commit -m "test(e2e): 引入编译错误 - 缺少分号"

push_to_fork "$BRANCH_NAME"

# ============================================================
# Phase 2: 创建 PR 并触发 CI
# ============================================================
log_step "Phase 2: 创建 PR 并触发 CI"

PR_NUMBER=$(create_pr "$BRANCH_NAME" "[E2E-TEST] 编译错误检测" "E2E 测试 - 编译错误场景")
trigger_pipeline "$PR_NUMBER"

# ============================================================
# Phase 3: 等待 CI 失败
# ============================================================
log_step "Phase 3: 等待 CI 失败"

PIPELINE_INFO=$(wait_pipeline_status "$PR_NUMBER" "failed|canceled")
PIPELINE_STATUS=$(echo "$PIPELINE_INFO" | grep -oP 'status=\K[^\s]+')
assert_eq "failed" "$PIPELINE_STATUS" "期望流水线失败"

# ============================================================
# Phase 4: 用 skill 脚本分析失败 - 严格按 SKILL.md 流程
# ============================================================
log_step "Phase 4: 分析流水线失败原因（严格按 skill 流程）"

# 步骤 5.1: 获取主流水线详情
log_info "步骤 5.1: 获取主流水线详情"
PID=$(echo "$PIPELINE_INFO" | grep -oP 'pipeline_id=\K[^\s]+')
PRID=$(echo "$PIPELINE_INFO" | grep -oP 'pipeline_run_id=\K[^\s]+')
DETAIL=$(echo "$PIPELINE_INFO" | sed 's/.*pipeline_detail=//')

DETAIL_OUTPUT=$(bash "${GP_SCRIPTS_DIR}/gp-detail.sh" "$PID" "$PRID" "$DETAIL")
log_info "主流水线详情:\n$DETAIL_OUTPUT"

# 验证流水线状态为 FAILED
assert_contains "$DETAIL_OUTPUT" "FAILED" "主流水线应为 FAILED"

# 找到失败的 Job (compile 相关)
FAILED_JOBS=$(echo "$DETAIL_OUTPUT" | grep "FAILED" | grep -i "compile")
assert_contains "$FAILED_JOBS" "compile" "应有 compile 相关 Job 失败"

# 获取子流水线信息 (compile 是 subPipeline 类型)
STEP_ID=$(echo "$FAILED_JOBS" | grep -oP 'step_id=\K[^\s]+' | head -1)
if [ -n "$STEP_ID" ]; then
  log_info "步骤 5.2: 穿透子流水线 (step_id=$STEP_ID)"
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

# 步骤 5.3: 获取失败 Job 的日志
log_info "步骤 5.3: 获取失败 Job 日志"
FAILED_LOG=$(bash "${GP_SCRIPTS_DIR}/gp-log.sh" "$LOG_PID" "$LOG_PRID" "$JOB_ID" "$DETAIL" 30)
log_info "失败日志:\n$FAILED_LOG"

# 验证日志中包含编译错误特征
# CI 日志中的编译错误可能是 "Error 2"、"error:" 或 "FAILED" 等多种形式
assert_contains "$FAILED_LOG" "error|Error|FAILED|failed" "日志应包含编译错误特征"

log_info "✅ Phase 4 通过: 成功检测到编译错误"

# ============================================================
# Phase 4.5: 用 API retry 重触流水线（验证 gp-api-retry.sh）
# ============================================================
log_step "Phase 4.5: 通过 API retry 重跑流水线（预期仍失败，验证 retry 功能）"

RETRY_OUTPUT=$(bash "${GP_SCRIPTS_DIR}/gp-api-retry.sh" "$PR_NUMBER" 2>&1) || true
log_info "API retry 输出:\n$RETRY_OUTPUT"

if echo "$RETRY_OUTPUT" | grep -qE "FAILED|PASSED|CANCELED"; then
  log_info "✅ API retry 成功触发并完成轮询"
else
  log_error "API retry 未返回预期结果"
  log_error "输出: $RETRY_OUTPUT"
  if $SKIP_CLEANUP; then
    log_warn "PR #${PR_NUMBER} 保留，请手动排查"
    exit 1
  fi
fi

assert_contains "$RETRY_OUTPUT" "FAILED" "重跑后应仍为 FAILED（代码未修复）"

RETRY_PIPELINE_INFO=$(bash "${GP_SCRIPTS_DIR}/gp-list.sh" "$PR_NUMBER" 2>/dev/null | head -1)
RETRY_STATUS=$(echo "$RETRY_PIPELINE_INFO" | grep -oP 'status=\K[^\s]+')
assert_eq "failed" "$RETRY_STATUS" "retry 后流水线应仍为 failed"

log_info "✅ Phase 4.5 通过: API retry 功能验证成功"

# QUICK_MODE: stop after detection + retry, skip fix cycle
if $QUICK_MODE; then
  log_info "✅ QUICK_MODE: 跳过修复→通过循环，skill 检测能力已验证"
  log_info "==========================================="
  log_info "✅ E2E 测试 [编译错误] 检测阶段通过 (QUICK_MODE)!"
  log_info "==========================================="
  exit 0
fi

# ============================================================
# Phase 5: 修复错误并推送
# ============================================================
log_step "Phase 5: 修复编译错误"

sed -i 's/DEF_ERRORNO(SUCCESS, "Success")/DEF_ERRORNO(SUCCESS, "Success");/' "$TARGET_FILE"
git add "$TARGET_FILE"
git commit -m "fix(e2e): 修复编译错误 - 恢复分号"
push_to_fork "$BRANCH_NAME"

# 等待 CI 重新触发（push 自动触发或手动触发）
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
  FINAL_LOG=$(bash "${GP_SCRIPTS_DIR}/gp-analyze-failure.sh" "$PR_NUMBER")
  log_error "失败详情:\n$FINAL_LOG"
  if $SKIP_CLEANUP; then
    log_warn "PR #${PR_NUMBER} 保留，请手动排查"
    exit 1
  fi
fi

assert_eq "success" "$FINAL_STATUS" "修复后 CI 应通过"

# 验证 ci-pipeline-passed label
sleep 30
if check_pr_label "$PR_NUMBER" "ci-pipeline-passed"; then
  log_info "✅ ci-pipeline-passed label 已添加"
else
  log_warn "ci-pipeline-passed label 未检测到（可能需要更多时间）"
fi

log_info "=========================================="
log_info "✅ E2E 测试 [编译错误] 全部通过!"
log_info "=========================================="
