#!/usr/bin/env bash
# common.sh - E2E 测试公共函数库
#
# 提供创建/删除 PR、轮询流水线状态、断言等公共操作

set -euo pipefail

# common.sh lives in tests/ directory, sibling to ../scripts/
_SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GP_SCRIPTS_DIR="${_SELF_DIR}/../scripts"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"

# Ensure we're in the git repo root (tests/ is inside the repo)
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
if [ -n "$REPO_ROOT" ] && [ "$(pwd)" != "$REPO_ROOT" ]; then
  cd "$REPO_ROOT"
fi

if [ -z "${GP_OWNER:-}" ] || [ -z "${GP_REPO:-}" ]; then
  repo_url=$(git remote get-url origin 2>/dev/null)
  if [[ "$repo_url" == git@* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
  else
    GP_OWNER=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
  fi
fi
ENCODED_REPO=$(printf '%s' "${GP_OWNER}/${GP_REPO}" | jq -sRr @uri)

POLL_INTERVAL=60
POLL_MAX_WAIT=3600

# QUICK_MODE: only verify skill detection (skip fix→push→wait pass cycle)
QUICK_MODE=false
[[ "${E2E_QUICK_MODE:-}" == "true" ]] && QUICK_MODE=true

log_info()  { echo -e "\033[32m[INFO]\033[0m $*" >&2; }
log_warn()  { echo -e "\033[33m[WARN]\033[0m $*" >&2; }
log_error() { echo -e "\033[31m[ERROR]\033[0m $*" >&2; }
log_step()  { echo -e "\033[36m[STEP]\033[0m $*" >&2; }

assert_eq() {
  local expected="$1" actual="$2" msg="${3:-}"
  if [ "$expected" != "$actual" ]; then
    log_error "断言失败: 期望='$expected' 实际='$actual' $msg"
    return 1
  fi
}

assert_contains() {
  local haystack="$1" needle="$2" msg="${3:-}"
  if echo "$haystack" | grep -qE "$needle"; then
    return 0
  fi
  log_error "断言失败: 字符串中未包含 '$needle' $msg"
  log_error "实际内容: $(echo "$haystack" | head -5)"
  return 1
}

assert_not_contains() {
  local haystack="$1" needle="$2" msg="${3:-}"
  if echo "$haystack" | grep -qE "$needle"; then
    log_error "断言失败: 字符串中不应包含 '$needle' $msg"
    return 1
  fi
}

save_original_branch() {
  ORIGINAL_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "HEAD")
  ORIGINAL_COMMIT=$(git rev-parse HEAD)
}

restore_original_branch() {
  if [ -n "${ORIGINAL_BRANCH:-}" ]; then
    git checkout "$ORIGINAL_BRANCH" >/dev/null 2>&1 || true
  fi
}

create_test_branch() {
  local test_name="$1"
  local base_branch="${2:-develop}"
  local branch_name="e2e-${test_name}-$(date +%Y%m%d%H%M%S)"

  log_step "创建测试分支: $branch_name (基于 $base_branch)"
  git fetch origin "${base_branch}" >/dev/null 2>&1 || true
  git checkout -b "$branch_name" "origin/${base_branch}" >/dev/null 2>&1 || \
    git checkout -b "$branch_name" "${base_branch}" >/dev/null 2>&1

  echo "$branch_name"
}

delete_test_branch() {
  local branch_name="$1"
  log_step "删除本地测试分支: $branch_name"
  git checkout "${ORIGINAL_BRANCH:-develop}" >/dev/null 2>&1 || true
  git branch -D "$branch_name" >/dev/null 2>&1 || true
  git push hgjupstream --delete "$branch_name" >/dev/null 2>&1 || true
}

create_pr() {
  local branch_name="$1"
  local title="$2"
  local body="${3:-E2E test PR - will be deleted after test}"

  log_step "创建 PR: $title"
  local response
  response=$(curl -s -X POST \
    "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pulls?access_token=${TOKEN}" \
    -H "Content-Type: application/json" \
    -d "$(jq -n \
      --arg title "$title" \
      --arg body "$body" \
      --arg head "stevenaw0:${branch_name}" \
      --arg base "develop" \
      '{title: $title, body: $body, head: $head, base: $base}')")

  local pr_number
  pr_number=$(echo "$response" | jq -r '.number // empty')
  if [ -z "$pr_number" ]; then
    log_error "创建 PR 失败: $(echo "$response" | jq -r '.message // .error // "unknown"' 2>/dev/null)"
    log_error "完整响应: $response"
    return 1
  fi

  log_info "PR #$pr_number 已创建"
  echo "$pr_number"
}

delete_pr() {
  local pr_number="$1"
  log_step "关闭并删除 PR #$pr_number"
  curl -s -X PATCH \
    "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pulls/${pr_number}?access_token=${TOKEN}" \
    -H "Content-Type: application/json" \
    -d '{"state":"closed"}' > /dev/null 2>&1 || true
}

trigger_pipeline() {
  local pr_number="$1"
  log_step "触发 PR #$pr_number 流水线"
  local result
  result=$(bash "${GP_SCRIPTS_DIR}/gp-trigger.sh" "$pr_number")
  log_info "触发结果: $result"
  sleep 15
}

wait_pipeline_status() {
  local pr_number="$1"
  local expected_status="${2:-success|failed|canceled}"
  local elapsed=0

  log_step "轮询 PR #$pr_number 流水线状态 (期望: $expected_status)..."
  while [ "$elapsed" -lt "$POLL_MAX_WAIT" ]; do
    local output
    output=$(bash "${GP_SCRIPTS_DIR}/gp-list.sh" "$pr_number" 2>/dev/null | head -1) || true
    local status
    status=$(echo "$output" | grep -oP 'status=\K[^\s]+' || true)

    if [ -n "$status" ] && echo "$status" | grep -qE "$expected_status"; then
      log_info "流水线状态: $status (${elapsed}s)"
      echo "$output"
      return 0
    fi

    log_info "... ${status:-pending} (${elapsed}s)"
    sleep "$POLL_INTERVAL"
    elapsed=$((elapsed + POLL_INTERVAL))
  done

  log_error "轮询超时 (${POLL_MAX_WAIT}s)"
  return 1
}

check_pr_label() {
  local pr_number="$1"
  local label="$2"
  local labels
  labels=$(curl -s "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pulls/${pr_number}?access_token=${TOKEN}" \
    | jq -r '.labels[]?.name' 2>/dev/null)
  echo "$labels" | grep -q "$label"
}

analyze_failure() {
  local pr_number="$1"
  log_step "分析 PR #$pr_number 流水线失败原因"
  bash "${GP_SCRIPTS_DIR}/gp-analyze-failure.sh" "$pr_number"
}

get_latest_pipeline_info() {
  local pr_number="$1"
  bash "${GP_SCRIPTS_DIR}/gp-list.sh" "$pr_number" 2>/dev/null | head -1
}

push_to_fork() {
  local branch_name="$1"
  log_step "推送分支到 fork: $branch_name"
  git push hgjupstream "$branch_name" --force
}

apply_fix_and_push() {
  local branch_name="$1"
  local fix_command="$2"
  local commit_msg="$3"

  log_step "应用修复: $commit_msg"
  eval "$fix_command"
  git add -A
  git commit -m "$commit_msg"
  push_to_fork "$branch_name"
}
