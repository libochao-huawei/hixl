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
# gp-trigger.sh - 触发 PR 流水线
#
# 用途: 通过发送 compile 评论触发 PR 的 CI 流水线
# 适用: 无流水线或需要重新触发时
#
# 入参:
#   $1  PR 编号 (必填)
#   $2  --force (可选) 跳过"正在运行"检查强制触发
#
# 评论内容固定为 "compile"，不接受自定义
#
# 【抢占保护】触发前必须先确认没有正在跑的流水线。
#   很多仓库的 workflow 配了：
#     concurrency:
#       preemption:
#         enable: true
#         events: [mr_id]
#   此时再评论 compile 会**取消**同一 PR 上正在运行的流水线 —— 包括一个可能所有 job 都已
#   通过、只差顶层收尾的 run。实测 Ascend/torchair PR#3725 因此连续损失两条全绿流水线
#   (#4120、#4122 均为「全部 job ✅ COMPLETED、顶层 🟨 CANCELED」)，白白多花约 20 分钟。
#   检查同时覆盖两套 CI：门禁 label（ci-pipeline-running）+ v8 Actions run 状态。
#   之所以必须看 label：被抢占的往往是 Legacy 流水线，而 Legacy 在部分仓库上 v5 接口
#   根本查不到（gp-list.sh 返回 0 条），只有 label 能反映它。
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 纯文本):
#   ok                       评论发送成功
#   error:<错误信息>          发送失败
# 拒绝时输出到 stderr 并以退出码区分:
#   0=已触发  1=用法/环境错  2=拒绝（CI 正在运行或 PR 已关闭/合入）
#
# 示例:
#   $ ./gp-trigger.sh 2479
#   ok
#
#   $ ./gp-trigger.sh 3725
#   refused: 门禁 label=ci-pipeline-running，CI 正在运行。
#            该仓库 workflow 可能配了 concurrency.preemption.events=[mr_id]，
#            再评论 compile 会抢占取消正在跑的流水线（可能是一个已全部 job 通过、只差收尾的 run）。
#            等它结束再触发，或加 --force 强制。

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-trigger.sh <PR_NUMBER> [--force]" >&2
  exit 1
fi

PR_NUMBER="$1"
FORCE=0
if [ -n "${2:-}" ]; then
  if [ "$2" = "--force" ]; then
    FORCE=1
  else
    echo "错误: 第二个参数只接受 --force（评论内容固定为 compile）" >&2
    exit 1
  fi
fi

COMMENT="compile"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "${GP_OWNER:-}" ] || [ -z "${GP_REPO:-}" ]; then
  repo_url=$(git remote get-url origin 2>/dev/null)
  if [[ "$repo_url" == git@* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
  elif [[ "$repo_url" == *gitcode.com/* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
  else
    echo "无法从 git remote 检测仓库信息，请设置 GP_OWNER 和 GP_REPO" >&2
    exit 1
  fi
fi

# ---- 抢占保护：触发前确认没有正在跑的流水线 ----
# 两个信号都要看：门禁 label 覆盖 Legacy（v5 接口在部分仓库查不到），v8 run 覆盖 Actions（label 可能滞后）
if [ "$FORCE" -eq 0 ]; then
  META=$(bash "${SCRIPT_DIR}/_gp_pr_meta.sh" "$PR_NUMBER" 2>/dev/null || true)
  PR_STATE=$(printf '%s\n' "$META" | sed -n 's/^state=//p')
  GATE=$(printf '%s\n' "$META" | sed -n 's/^gate=//p')
  LEGACY=$(printf '%s\n' "$META" | sed -n 's/^legacy_report=//p')
  V8=$(bash "${SCRIPT_DIR}/gpv8-list.sh" "$PR_NUMBER" 2>/dev/null || true)

  case "$PR_STATE" in
    merged | closed)
      echo "refused: PR ${PR_NUMBER} 已 ${PR_STATE}，触发流水线无意义。" >&2
      exit 2
      ;;
  esac

  if [ "$GATE" = "running" ] || printf '%s' "$V8" | grep -q "status=RUNNING"; then
    echo "refused: CI 正在运行，触发会抢占取消它。" >&2
    echo "  门禁 label   = ci-pipeline-running" >&2
    [ -n "$V8" ] && echo "  v8 Actions   = $V8" >&2
    [ -n "$LEGACY" ] && echo "  Legacy 报告  = $LEGACY" >&2
    echo "  该仓库 workflow 可能配了 concurrency.preemption.events=[mr_id]，" >&2
    echo "  被取消的可能是一个所有 job 已通过、只差顶层收尾的 run。" >&2
    echo "  等它结束再触发（可用 gp-gate-wait.sh 轮询），或加 --force 强制。" >&2
    exit 2
  fi
fi

RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pulls/${PR_NUMBER}/comments" \
  -d "{\"body\": \"${COMMENT}\"}")

if [ "$RESPONSE" -ge 200 ] && [ "$RESPONSE" -lt 300 ]; then
  echo "ok"
else
  echo "error: HTTP $RESPONSE"
  exit 1
fi
