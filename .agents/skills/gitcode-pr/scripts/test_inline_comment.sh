#!/bin/bash
# 测试 v5 API 和 web-api 的行内评论功能（包括多行选择）
# 基于官方 HTML 文档和用户提供的 web-api 示例

set -e

# 配置
REPO_OWNER="cann"
REPO_NAME="ge"
PR_NUMBER="2805"
ENCODED_REPO="cann%2Fge"
TOKEN="${GITCODE_API_TOKEN}"

if [ -z "$TOKEN" ]; then
    echo "❌ 错误：GITCODE_API_TOKEN 未设置"
    echo "请设置环境变量：export GITCODE_API_TOKEN='your_token'"
    exit 1
fi

echo "=== GitCode 行内评论 API 对比测试 ==="
echo "仓库: ${REPO_OWNER}/${REPO_NAME}"
echo "PR: #${PR_NUMBER}"
echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# 获取 PR 信息
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "准备阶段：获取 PR 基本信息"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

PR_INFO=$(curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}?access_token=$TOKEN")

if echo "$PR_INFO" | jq -e '.number' > /dev/null 2>&1; then
    BASE_SHA=$(echo "$PR_INFO" | jq -r '.base.sha')
    HEAD_SHA=$(echo "$PR_INFO" | jq -r '.head.sha')
    echo "Base SHA: $BASE_SHA"
    echo "Head SHA: $HEAD_SHA"
else
    echo "❌ 获取 PR 信息失败"
    exit 1
fi

# 获取变更文件列表
FILES=$(curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/files.json?access_token=$TOKEN")

if echo "$FILES" | jq -e '.diffs[0].statistic.new_path' > /dev/null 2>&1; then
    FIRST_FILE=$(echo "$FILES" | jq -r '.diffs[0].statistic.new_path')
    # 获取正确的 base_sha 和 head_sha
    DIFF_BASE_SHA=$(echo "$FILES" | jq -r '.diff_refs.base_sha')
    DIFF_HEAD_SHA=$(echo "$FILES" | jq -r '.diff_refs.head_sha')
    DIFF_START_SHA=$(echo "$FILES" | jq -r '.diff_refs.start_sha')
    echo "第一个变更文件: $FIRST_FILE"
    echo "Diff Base SHA: $DIFF_BASE_SHA"
    echo "Diff Head SHA: $DIFF_HEAD_SHA"
    echo "Diff Start SHA: $DIFF_START_SHA"
    # 更新 SHA 变量（使用 diff_refs 中的值）
    BASE_SHA="$DIFF_BASE_SHA"
    HEAD_SHA="$DIFF_HEAD_SHA"
    START_SHA="$DIFF_START_SHA"
else
    echo "❌ 获取文件列表失败"
    exit 1
fi

echo ""

# 测试 1: 官方 v5 API - 单行评论
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 1: 官方 v5 API - 单行评论"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  -d '{
    "body": "官方 v5 API 单行评论测试 - 测试后将被删除",
    "path": "'"$FIRST_FILE"'",
    "position": 70
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "请求参数："
echo "  - body: 评论内容"
echo "  - path: $FIRST_FILE"
echo "  - position: 70"
echo ""
echo "HTTP 状态码: $HTTP_CODE"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    COMMENT_ID=$(echo "$BODY" | jq -r '.id')
    echo "✅ 成功创建单行评论"
    echo "评论 ID: $COMMENT_ID"
    echo "响应示例:"
    echo "$BODY" | jq '{id, body, path, position}'
    
    # 清理
    curl -s -X DELETE "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/comments/${COMMENT_ID}?access_token=$TOKEN" > /dev/null
    echo "✅ 测试评论已删除"
else
    echo "❌ 创建失败"
    echo "响应: $BODY"
fi

echo ""

# 测试 2: 官方 v5 API - 尝试多行（未文档化参数）
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 2: 官方 v5 API - 尝试多行（未文档化参数）"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  -d '{
    "body": "v5 API 多行测试（尝试未文档化参数）- 测试后将被删除",
    "path": "'"$FIRST_FILE"'",
    "position": 75,
    "start_position": 70
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "请求参数："
echo "  - body: 评论内容"
echo "  - path: $FIRST_FILE"
echo "  - position: 75"
echo "  - start_position: 70（未文档化）"
echo ""
echo "HTTP 状态码: $HTTP_CODE"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    COMMENT_ID=$(echo "$BODY" | jq -r '.id')
    echo "✅ 未文档化参数可能有效（需要验证实际效果）"
    echo "评论 ID: $COMMENT_ID"
    
    # 清理
    curl -s -X DELETE "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/comments/${COMMENT_ID}?access_token=$TOKEN" > /dev/null
    echo "✅ 测试评论已删除"
else
    echo "⚠️ v5 API 不支持多行（未文档化参数无效）"
    echo "响应: $BODY"
fi

echo ""

# 测试 3: web-api 端点 - 多行评论（用户提供的特殊 API）
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 3: web-api 端点 - 多行评论（用户提供的特殊格式）"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "PRIVATE-TOKEN: $TOKEN" \
  -H "Content-Type: application/json" \
  "https://web-api.gitcode.com/issuepr/api/v1/projects/${ENCODED_REPO}/merge_requests/${PR_NUMBER}/discussions" \
  -d '{
    "repoId": "'"${ENCODED_REPO}"'",
    "iid": '"${PR_NUMBER}"',
    "body": "web-api 端点多行评论测试（第 70-75 行）- 测试后将被删除",
    "line_types": "new",
    "position": {
      "base_sha": "'"${BASE_SHA}"'",
      "start_sha": "'"${BASE_SHA}"'",
      "head_sha": "'"${HEAD_SHA}"'",
      "position_type": "text",
      "new_path": "'"${FIRST_FILE}"'",
      "old_path": "'"${FIRST_FILE}"'",
      "new_line": 75,
      "start_new_line": 70,
      "old_line": -1,
      "ignore_whitespace_change": false
    },
    "severity": "suggestion"
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "请求参数："
echo "  - repoId: ${ENCODED_REPO}"
echo "  - iid: ${PR_NUMBER}"
echo "  - body: 评论内容"
echo "  - line_types: new"
echo "  - position.new_line: 75"
echo "  - position.start_new_line: 70"
echo "  - severity: suggestion"
echo ""
echo "HTTP 状态码: $HTTP_CODE"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    echo "✅ web-api 端点支持多行评论"
    echo "响应示例:"
    echo "$BODY" | jq '{id, body}' 2>/dev/null || echo "$BODY"
    
    # 注意：web-api 端点的删除操作可能需要不同的端点
    echo "注意：web-api 创建的评论可能需要手动删除或在 PR 页面删除"
else
    echo "❌ web-api 端点不可用或认证失败"
    echo "响应: $BODY"
fi

echo ""

# 测试 4: 对比总结
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "API 对比总结"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

echo ""
echo "官方 v5 API（基于 HTML 文档）："
echo "  端点: https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments"
echo "  参数: body (必填), path (可选), position (可选)"
echo "  认证: access_token query 参数"
echo "  优点: 简单、标准、官方文档支持"
echo "  缺点: 未明确支持多行选择（需要验证未文档化参数）"
echo ""

echo "web-api 端点（用户示例）："
echo "  端点: https://web-api.gitcode.com/issuepr/api/v1/projects/:repoId/merge_requests/:iid/discussions"
echo "  参数: 复杂 position 对象，包含 start_new_line"
echo "  认证: PRIVATE-TOKEN header"
echo "  优点: 明确支持多行选择"
echo "  缺点: 参数复杂，可能是非标准 API"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "迁移建议"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "1. 普通评论和单行评论：使用官方 v5 API（简单可靠）"
echo "2. 多行评论：使用 web-api 端点（明确支持）"
echo "3. 如果 web-api 不可用：考虑多次单行评论替代方案"
echo ""
echo "测试完成！"