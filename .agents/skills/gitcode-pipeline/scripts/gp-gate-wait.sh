#!/usr/bin/env bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
# gp-gate-wait.sh - 以「门禁 label」为终态轮询 PR，直到可合入或明确失败
#
# 用途: 盯 CI 的首选轮询脚本。与 gpv8-wait.sh / gp-wait.sh 的区别是终态判据不同：
#       那两个脚本盯的是**某一套 CI 系统的 run status**，本脚本盯的是**门禁 label**。
#
# 为什么需要它（实测踩坑）:
#   双 CI 并存的仓库里，单系统 run status 会给出错误结论。Ascend/torchair PR#3725：
#     - v8 Actions run #64/#66/#67 三次 FAILED（sca-pr / malicious-scan 报
#       "当前扫描仓库不在openlibing中"，平台侧配置缺失，重试不可恢复）
#     - 同一时间 Legacy 流水线 #4123 全部 job ✅、顶层 ✅，门禁 label 置为 ci-pipeline-passed
#   gpv8-wait.sh 在 09:31:54 就 TERMINAL:FAILED 退出了，而 label 09:44 才变绿。
#   只信它会去修一个根本不存在的代码问题。
#
# 适用: 步骤 4（循环查询流水线状态）。仅当 gp-detect.sh 确认是单系统仓库时，
#       才退而使用 gpv8-wait.sh / gp-wait.sh。
#
# 入参:
#   $1  PR 编号 (必填)
#   $2  超时分钟数 (可选，默认 60)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER / GP_REPO (可选，默认从 git remote 自动检测；检测失败时可显式设置以便在任意目录运行)
#   GP_POLL_INTERVAL   (可选，轮询间隔秒，默认 60)
#
# 返回值 (stdout):
#   轮询中: 每个间隔一行
#     [HH:MM:SS] gate=running state=open | v8 run#67=FAILED | legacy PR-pipeline_torchair#4123=已完成 | approval=lgtm:2/2,approve:1/1
#   终态:   打印「门禁面板」（见下），再按结果退出
#
# 退出码:
#   0 = gate=passed，或 PR 已 merged/closed
#   1 = gate=failed
#   2 = 审批未满足（gate 已 passed 但 lgtm/approve 不够，或存在硬阻塞 label）
#   3 = 超时仍未到终态
#   4 = 用法/环境错或接口取不到数据
#
# 门禁面板示例（终态时输出，覆盖步骤 5「成功则报告」与步骤 10「检查其他阻塞项」）:
#   ==== 门禁面板 ====
#   gate        = passed ✅
#   PR 状态     = merged | mergeable = true
#   head_sha    = c9e343d29506efd479ca5db88d45393c57afe242
#   审批        = lgtm 2/2 ✅ | approve 1/1 ✅
#   硬阻塞      = 无
#   提示        = stat/needs-squash（可能由平台 merge 时 squash 消化）
#   Legacy      = PR-pipeline_torchair#4123 sha=c9e343d2 status=已完成
#   v8 Actions  = run#67 status=FAILED  ⚠️ 与门禁结论不一致，见下方说明
#
# 注意:
#   1. 调用此脚本建议设置 60 分钟超时（3600000ms）
#   2. v8 run 的 head_sha 是合并预览 SHA，与 PR head.sha 不相等属正常，禁止用 SHA 相等判断归属
#   3. v8 与门禁结论不一致时（v8 FAILED 但 gate=passed），面板会打 ⚠️ 并提示：
#      多半是 Actions 侧某个 job 依赖的外部服务未对该仓库开通，属平台配置问题，
#      用 gpv8-detail.sh + gpv8-log.sh 取该 job 日志确认后按「环境问题」上报，不要改代码

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-gate-wait.sh <PR_NUMBER> [TIMEOUT_MINUTES]" >&2
  exit 4
fi

PR_NUMBER="$1"
TIMEOUT_MIN="${2:-60}"
INTERVAL="${GP_POLL_INTERVAL:-60}"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "${GP_OWNER:-}" ] || [ -z "${GP_REPO:-}" ]; then
  repo_url=$(git remote get-url origin 2>/dev/null || true)
  if [[ "$repo_url" == git@* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
  elif [[ "$repo_url" == *gitcode.com/* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
  else
    echo "无法从 git remote 检测仓库信息，请设置 GP_OWNER 和 GP_REPO" >&2
    exit 4
  fi
fi
export GP_OWNER GP_REPO

meta_field() { printf '%s\n' "$META" | sed -n "s/^$1=//p"; }

# 审批是否满足：approval=lgtm:2/2,approve:1/1 → 每项 n>=m
# 全程用 if 而不用 `[ ] && xxx`：本脚本 set -euo pipefail，测试为假时 && 链返回 1 会直接退出脚本
approval_ok() {
  local a="$1" item n m
  if [ "$a" = "unknown" ] || [ -z "$a" ]; then
    return 1
  fi
  IFS=',' read -ra parts <<<"$a"
  for item in "${parts[@]}"; do
    n=$(printf '%s' "$item" | sed -E 's/.*:([0-9]+)\/[0-9]+/\1/')
    m=$(printf '%s' "$item" | sed -E 's/.*:[0-9]+\/([0-9]+)/\1/')
    if [ -z "$n" ] || [ -z "$m" ]; then
      return 1
    fi
    if [ "$n" -lt "$m" ]; then
      return 1
    fi
  done
  return 0
}

# 把 approval 串渲染成带勾叉的可读形式
approval_render() {
  local a="$1" item name n m mark out=""
  if [ "$a" = "unknown" ] || [ -z "$a" ]; then
    echo "unknown（未取到 ascend-robot 的 PR Approval Progress 评论）"
    return 0
  fi
  IFS=',' read -ra parts <<<"$a"
  for item in "${parts[@]}"; do
    name=${item%%:*}
    n=$(printf '%s' "$item" | sed -E 's/.*:([0-9]+)\/[0-9]+/\1/')
    m=$(printf '%s' "$item" | sed -E 's/.*:[0-9]+\/([0-9]+)/\1/')
    mark="❌"
    if [ -n "$n" ] && [ -n "$m" ] && [ "$n" -ge "$m" ]; then
      mark="✅"
    fi
    out="${out:+$out | }${name} ${n}/${m} ${mark}"
  done
  echo "$out"
  return 0
}

print_panel() {
  local gate="$1" state="$2" mergeable="$3" sha="$4" approval="$5" blocking="$6" advisory="$7" legacy="$8" v8="$9"
  echo ""
  echo "==== 门禁面板 ===="
  case "$gate" in
    passed) echo "gate        = passed ✅" ;;
    failed) echo "gate        = failed ❌" ;;
    running) echo "gate        = running 🟣" ;;
    *) echo "gate        = ${gate}（无 ci-pipeline-* label，CI 可能尚未触发）" ;;
  esac
  echo "PR 状态     = ${state} | mergeable = ${mergeable}"
  echo "head_sha    = ${sha}"
  echo "审批        = $(approval_render "$approval")"
  echo "硬阻塞      = ${blocking:-无}"
  echo "提示        = ${advisory:-无}"
  if [ -n "$advisory" ]; then
    echo "              （提示项可能由平台在 merge 时自动消化，实测 Ascend/torchair PR#3725 带 stat/needs-squash 仍成功合入）"
  fi
  echo "Legacy      = ${legacy:-（v5 接口与评论里都没有 Legacy 流水线报告）}"
  if [ -n "$v8" ]; then
    echo "v8 Actions  = $v8"
    # v8 可能是完整 gpv8-list 输出（status=FAILED）或面板用的短形式（run#67=FAILED），两种都要认
    if printf '%s' "$v8" | grep -qE '(status=|=)FAILED' && [ "$gate" = "passed" ]; then
      echo "              ⚠️ v8 run 失败但门禁已通过 —— 两套 CI 结论不一致。"
      echo "                 多半是 Actions 侧某 job 依赖的外部服务未对该仓库开通（平台配置问题）。"
      echo "                 用 gpv8-detail.sh <run_id> + gpv8-log.sh <run_id> <job_id> 取日志确认后按环境问题上报，不要改代码。"
    fi
  else
    echo "v8 Actions  = （无运行记录）"
  fi
  return 0
}

DEADLINE=$(( $(date +%s) + TIMEOUT_MIN * 60 ))
echo "开始轮询门禁（每 ${INTERVAL}s，超时 ${TIMEOUT_MIN} 分钟）..."

while true; do
  TS=$(date '+%H:%M:%S')

  if ! META=$(bash "${SCRIPT_DIR}/_gp_pr_meta.sh" "$PR_NUMBER" 2>/dev/null); then
    echo "[${TS}] 取门禁信息失败（_gp_pr_meta.sh 非零退出），${INTERVAL}s 后重试..."
    if [ "$(date +%s)" -ge "$DEADLINE" ]; then
      echo "TERMINAL:TIMEOUT"
      exit 3
    fi
    sleep "$INTERVAL"
    continue
  fi

  GATE=$(meta_field gate)
  STATE=$(meta_field state)
  MERGEABLE=$(meta_field mergeable)
  SHA=$(meta_field head_sha)
  APPROVAL=$(meta_field approval)
  BLOCKING=$(meta_field blocking)
  ADVISORY=$(meta_field advisory)
  LEGACY=$(meta_field legacy_report)

  V8=$(bash "${SCRIPT_DIR}/gpv8-list.sh" "$PR_NUMBER" 2>/dev/null || true)
  V8_SHORT=""
  if [ -n "$V8" ]; then
    V8_SHORT="run#$(printf '%s' "$V8" | grep -oP 'run_number=\K[^ ]+')=$(printf '%s' "$V8" | grep -oP 'status=\K[^ ]+')"
  fi
  LEGACY_SHORT=""
  if [ -n "$LEGACY" ]; then
    LEGACY_SHORT="$(printf '%s' "$LEGACY" | grep -oE '^[^ ]+#?[0-9]*' | head -1)=$(printf '%s' "$LEGACY" | sed -n 's/.*status=//p')"
  fi

  echo "[${TS}] gate=${GATE} state=${STATE} | v8 ${V8_SHORT:-无记录} | legacy ${LEGACY_SHORT:-无报告} | approval=${APPROVAL}"

  # ---- 终态判定 ----
  case "$STATE" in
    merged | closed)
      echo "TERMINAL:${STATE^^}"
      print_panel "$GATE" "$STATE" "$MERGEABLE" "$SHA" "$APPROVAL" "$BLOCKING" "$ADVISORY" "$LEGACY" "$V8_SHORT"
      echo ""
      echo "PR 已 ${STATE}，无需继续等待。"
      exit 0
      ;;
  esac

  if [ "$GATE" = "passed" ]; then
    echo "TERMINAL:PASSED"
    print_panel "$GATE" "$STATE" "$MERGEABLE" "$SHA" "$APPROVAL" "$BLOCKING" "$ADVISORY" "$LEGACY" "$V8_SHORT"
    # gate 通过 ≠ 可合入：还要看硬阻塞 label 与审批进度
    if [ -n "$BLOCKING" ]; then
      echo ""
      echo "❌ 仍存在硬阻塞 label: ${BLOCKING}"
      exit 2
    fi
    if ! approval_ok "$APPROVAL"; then
      echo ""
      echo "❌ CI 已通过，但审批未满足或取不到审批进度: ${APPROVAL}"
      exit 2
    fi
    echo ""
    echo "✅ CI 通过且无硬阻塞、审批已满足。"
    exit 0
  fi

  if [ "$GATE" = "failed" ]; then
    echo "TERMINAL:FAILED"
    print_panel "$GATE" "$STATE" "$MERGEABLE" "$SHA" "$APPROVAL" "$BLOCKING" "$ADVISORY" "$LEGACY" "$V8_SHORT"
    echo ""
    echo "下一步定位失败："
    echo "  1) bash gp-detect.sh ${PR_NUMBER}          # 确认该用哪套脚本"
    echo "  2) bash gp-comments.sh ${PR_NUMBER}        # Legacy：从评论读各 job 状态表（v5 接口查不到时唯一途径）"
    echo "  3) mode=actions → gpv8-list.sh / gpv8-detail.sh / gpv8-log.sh"
    echo "     mode=legacy  → gp-list.sh / gp-detail.sh / gp-log.sh（或 gp-analyze-failure.sh 一键穿透）"
    exit 1
  fi

  if [ "$(date +%s)" -ge "$DEADLINE" ]; then
    echo "TERMINAL:TIMEOUT"
    print_panel "$GATE" "$STATE" "$MERGEABLE" "$SHA" "$APPROVAL" "$BLOCKING" "$ADVISORY" "$LEGACY" "$V8_SHORT"
    echo ""
    echo "⏱  ${TIMEOUT_MIN} 分钟内门禁仍未定（gate=${GATE}）。可加大超时重跑，或用 gp-comments.sh 看各系统进度。"
    exit 3
  fi

  sleep "$INTERVAL"
done
