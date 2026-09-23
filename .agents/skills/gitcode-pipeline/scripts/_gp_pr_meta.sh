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
# _gp_pr_meta.sh - 读取 PR 的门禁元信息（内部 helper）
#
# 用途: 一次请求拿齐 labels / head sha / 门禁状态 / 阻塞项 / 审批进度 / 最新机器人流水线报告。
#       供 gp-trigger.sh（抢占保护）、gp-gate-wait.sh（门禁轮询）共用，
#       避免每个脚本各写一遍 curl+jq 和 label 判定逻辑。
#
# 为什么需要它:
#   1. 门禁的权威判据是 label（ci-pipeline-passed/failed/running），不是任一系统的 run status。
#      双 CI 并存的仓库（如 Ascend/torchair）里，v8 Actions 的 run 可能永久失败在平台侧
#      未注册的 job 上，而真正决定能否合入的 Legacy 流水线是绿的。
#   2. Legacy 流水线在部分仓库上 v5 接口查不到（gp-list.sh 返回 0 条），
#      其状态只能从 AtlasAccount 的 PR 评论里读。
#   3. 审批进度（lgtm/approve）只存在于 ascend-robot 评论的表格里，label 里没有。
#
# 入参:
#   $1  PR 编号 (必填)
#   $2  字段名 (可选): labels|head_sha|gate|blocking|advisory|approval|legacy_report|state|mergeable
#       省略时输出全部字段（key=value 每行一项）
#
# 环境变量:
#   GITCODE_API_TOKEN    (必填)
#   GP_OWNER / GP_REPO   (可选，默认从 git remote 自动检测；检测失败时可显式设置以便在任意目录运行)
#   GP_BLOCKING_LABELS   (可选，空格分隔的硬阻塞 label 清单，覆盖内置默认值)
#   GP_ADVISORY_LABELS   (可选，空格分隔的提示 label 清单，覆盖内置默认值)
#
# 输出字段说明:
#   gate=passed|failed|running|unknown   由 ci-pipeline-* label 推导
#   blocking=<空格分隔>                  硬阻塞 label，非空即不可合入
#                                        默认: ci-pipeline-failed needs-issue do-not-merge hold
#   advisory=<空格分隔>                  提示 label，可能由平台 merge 时自动消化
#                                        默认: stat/needs-squash
#   approval=lgtm:<n>/<m>,approve:<n>/<m>  取不到时为 approval=unknown
#   legacy_report=<流水线名#号 sha=xxx status=xxx>  取不到时为空
#                                        优先取真 CI 流水线报告；smoke 流水线（*_smoke，
#                                        label 为 smoke-pipeline-*）不是门禁 CI，只会以
#                                        "(smoke, 非真 CI)" 标注兜底输出。docs-ci-pipeline-*
#                                        同理是文档 CI，不参与门禁。
#
# 退出码: 0=成功  1=用法/环境错  3=接口无该 PR 数据
#
# 示例（取自 Ascend/torchair PR#3725 合入后的真实输出）:
#   $ ./_gp_pr_meta.sh 3725
#   state=merged
#   mergeable=true
#   head_sha=c9e343d29506efd479ca5db88d45393c57afe242
#   labels=stat/needs-squash ascend-cla/yes docs-ci-pipeline-success ci-pipeline-passed approved lgtm
#   gate=passed
#   blocking=
#   advisory=stat/needs-squash
#   approval=lgtm:2/2,approve:1/1
#   legacy_report=PR-pipeline_torchair#4123 sha=c9e343d2 status=已完成
#
#   $ ./_gp_pr_meta.sh 3725 gate
#   passed

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: _gp_pr_meta.sh <PR_NUMBER> [FIELD]" >&2
  exit 1
fi

PR_NUMBER="$1"
FIELD="${2:-}"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"

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
    exit 1
  fi
fi

V5="https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}"
AUTH="Authorization: Bearer ${TOKEN}"

# 阻塞 label 清单，分两级：
#   blocking  硬阻塞，命中即不可合入
#   advisory  提示项，可能由平台在 merge 时自动消化
#     （实测 Ascend/torchair PR#3725 带着 stat/needs-squash 仍成功合入，故降级为提示）
DEFAULT_BLOCKING="ci-pipeline-failed needs-issue do-not-merge hold"
DEFAULT_ADVISORY="stat/needs-squash"
BLOCKING_LIST="${GP_BLOCKING_LABELS:-$DEFAULT_BLOCKING}"
ADVISORY_LIST="${GP_ADVISORY_LABELS:-$DEFAULT_ADVISORY}"

pr_json=$(curl -s "${V5}/pulls/${PR_NUMBER}" -H "$AUTH" || true)
if [ -z "$pr_json" ] || ! echo "$pr_json" | jq -e '.number' >/dev/null 2>&1; then
  echo "no_record: v5 pulls 接口未返回 PR ${PR_NUMBER} 的数据（检查 PR 号与 GP_OWNER/GP_REPO）" >&2
  exit 3
fi

# 每处 jq 赋值都带 || true：本脚本 set -euo pipefail，jq 一旦非零（字段缺失/结构变化）
# 会让赋值语句返回非零并直接退出脚本，表现为"什么都不输出"。上面已用 jq -e '.number' 校验过
# 响应是合法 PR 对象，这里只是兜底，避免本 helper 成为所有调用方的单点猝死源。
state=$(echo "$pr_json" | jq -r '.state // "unknown"' 2>/dev/null || echo "unknown")
mergeable=$(echo "$pr_json" | jq -r '.mergeable // "unknown"' 2>/dev/null || echo "unknown")
head_sha=$(echo "$pr_json" | jq -r '.head.sha // ""' 2>/dev/null || true)
labels=$(echo "$pr_json" | jq -r '[.labels[]? | if type=="object" then .name else . end] | join(" ")' 2>/dev/null || true)

# gate：由 ci-pipeline-* label 推导
gate="unknown"
for l in $labels; do
  case "$l" in
    ci-pipeline-passed) gate="passed" ;;
    ci-pipeline-failed) [ "$gate" != "passed" ] && gate="failed" || true ;;
    ci-pipeline-running) [ "$gate" = "unknown" ] && gate="running" || true ;;
  esac
done

# blocking / advisory：labels 与两份清单的交集
# 注意 `|| true`：set -euo pipefail 下，[ ] 为假会让循环体最后一条命令返回 1 并直接退出脚本
blocking=""
advisory=""
for l in $labels; do
  for b in $BLOCKING_LIST; do
    [ "$l" = "$b" ] && blocking="${blocking:+$blocking }$l" || true
  done
  for a in $ADVISORY_LIST; do
    [ "$l" = "$a" ] && advisory="${advisory:+$advisory }$l" || true
  done
done

# 评论：用于提取审批进度与 Legacy 流水线报告
comments_json=$(curl -s "${V5}/pulls/${PR_NUMBER}/comments?per_page=100&page=1" -H "$AUTH" || true)

# 审批进度：取最后一条含 "PR Approval Progress" 的 ascend-robot 评论，抽 (n/m) 数字对
approval="unknown"
prog_body=$(echo "$comments_json" | jq -r '[.[] | select((.body // "") | contains("PR Approval Progress"))] | last | .body // ""' 2>/dev/null || true)
if [ -n "$prog_body" ]; then
  pairs=$(printf '%s' "$prog_body" | grep -oE '\([0-9]+/[0-9]+\)' | tr -d '()' || true)
  n1=$(printf '%s\n' "$pairs" | sed -n '1p')
  n2=$(printf '%s\n' "$pairs" | sed -n '2p')
  if [ -n "${n1:-}" ] && [ -n "${n2:-}" ]; then
    approval="lgtm:${n1},approve:${n2}"
  fi
fi

# Legacy 流水线报告：扫描 AtlasAccount 评论，抽 流水线名#号 / commitID / 顶层状态。
# ⚠️ smoke/docs 流水线不是真 CI：Ascend/torchair 等仓库上 PR 一创建就会自动跑
#    PR-pipeline_<repo>_smoke（label 为 smoke-pipeline-*），文档 PR 还有 docs-ci-pipeline-*。
#    它们不参与门禁（门禁只认 ci-pipeline-* label），真 CI 需要评论 compile 触发。
#    因此优先取名字不含 _smoke 的报告；只有 smoke 报告时带 (smoke, 非真 CI) 标注，
#    避免调用方把 smoke 的「运行中」误判为真 CI 正在跑而跳过触发。
legacy_report=""
atl_bodies=$(echo "$comments_json" | jq -r '[.[] | select((.user.login // "") == "AtlasAccount") | .body // ""] | reverse | .[]' 2>/dev/null || true)
smoke_report=""
while IFS= read -r body; do
  [ -n "$body" ] || continue
  atl_text=$(printf '%s' "$body" | sed 's/<[^>]*>/ /g' | tr -s ' ')
  pl_name=$(printf '%s' "$atl_text" | grep -oE '流水线 [^ ]+#[0-9]+' | head -1 | sed 's/^流水线 //' || true)
  [ -n "$pl_name" ] || continue
  pl_sha=$(printf '%s' "$atl_text" | grep -oE 'commitID：[0-9a-f]+' | head -1 | sed 's/^commitID：//' || true)
  pl_state=$(printf '%s' "$atl_text" | grep -oE '(运行失败|已完成|运行中|已终止运行)' | head -1 || true)
  if [[ "$pl_name" == *_smoke* ]]; then
    [ -n "$smoke_report" ] || smoke_report="${pl_name} sha=${pl_sha} status=${pl_state} (smoke, 非真 CI)"
  else
    legacy_report="${pl_name} sha=${pl_sha} status=${pl_state}"
    break
  fi
done <<< "$atl_bodies"
if [ -z "$legacy_report" ]; then
  legacy_report="$smoke_report"
fi

emit_all() {
  echo "state=${state}"
  echo "mergeable=${mergeable}"
  echo "head_sha=${head_sha}"
  echo "labels=${labels}"
  echo "gate=${gate}"
  echo "blocking=${blocking}"
  echo "advisory=${advisory}"
  echo "approval=${approval}"
  echo "legacy_report=${legacy_report}"
}

if [ -z "$FIELD" ]; then
  emit_all
else
  case "$FIELD" in
    state) echo "$state" ;;
    mergeable) echo "$mergeable" ;;
    head_sha) echo "$head_sha" ;;
    labels) echo "$labels" ;;
    gate) echo "$gate" ;;
    blocking) echo "$blocking" ;;
    advisory) echo "$advisory" ;;
    approval) echo "$approval" ;;
    legacy_report) echo "$legacy_report" ;;
    *)
      echo "未知字段: $FIELD（可用: state|mergeable|head_sha|labels|gate|blocking|advisory|approval|legacy_report）" >&2
      exit 1
      ;;
  esac
fi
