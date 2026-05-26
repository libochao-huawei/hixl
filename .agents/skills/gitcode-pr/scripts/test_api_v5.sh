#!/bin/bash
# GitCode API v5 测试脚本（基于官方文档）
# 测试所有基本的 v5 API 功能

set -e

# 配置
REPO_OWNER="cann"
REPO_NAME="ge"
PR_NUMBER="2805"
TOKEN="${GITCODE_API_TOKEN}"

if [ -z "$TOKEN" ]; then
    echo "❌ 错误：GITCODE_API_TOKEN 未设置"
    echo "请设置环境变量：export GITCODE_API_TOKEN='your_token'"
    exit 1
fi

echo "=== 测试 GitCode API v5 ==="
echo "仓库: ${REPO_OWNER}/${REPO_NAME}"
echo "PR: #${PR_NUMBER}"
echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# 测试 1: 获取 PR 信息（v5 API）
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 1: 获取 PR 信息"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

PR_INFO=$(curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}?access_token=$TOKEN")

if echo "$PR_INFO" | jq -e '.number' > /dev/null 2>&1; then
    echo "$PR_INFO" | jq '{number, title, state, base_sha: .base.sha, head_sha: .head.sha}'
    BASE_SHA=$(echo "$PR_INFO" | jq -r '.base.sha')
    HEAD_SHA=$(echo "$PR_INFO" | jq -r '.head.sha')
    echo "✅ 测试通过"
else
    echo "❌ 测试失败"
    echo "响应: $PR_INFO"
    exit 1
fi

# 测试 2: 获取 PR 文件变更（v5 API - 来自官方文档）
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 2: 获取 PR 文件变更（files.json）"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

FILES=$(curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/files.json?access_token=$TOKEN")

if echo "$FILES" | jq -e '.diffs[0].statistic.new_path' > /dev/null 2>&1; then
    FILE_COUNT=$(echo "$FILES" | jq '.diffs | length')
    FIRST_FILE=$(echo "$FILES" | jq -r '.diffs[0].statistic.new_path')
    echo "文件数量: $FILE_COUNT"
    echo "第一个文件: $FIRST_FILE"
    echo "$FILES" | jq '.diffs[0].statistic | {new_path, added_lines, remove_lines}'
    echo "✅ 测试通过"
else
    echo "❌ 测试失败"
    echo "响应: $FILES"
    exit 1
fi

# 测试 3: 获取 PR 评论列表（v5 API - 来自官方文档）
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 3: 获取 PR 评论列表"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

COMMENTS=$(curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN")

if echo "$COMMENTS" | jq -e 'length' > /dev/null 2>&1; then
    COMMENT_COUNT=$(echo "$COMMENTS" | jq 'length')
    echo "评论数量: $COMMENT_COUNT"
    if [ "$COMMENT_COUNT" -gt 0 ]; then
        echo "$COMMENTS" | jq '.[0] | {id, body, path, position}'
    fi
    echo "✅ 测试通过"
else
    echo "❌ 测试失败"
    echo "响应: $COMMENTS"
    exit 1
fi

# 测试 4: 提交普通评论（v5 API - 来自官方文档）
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 4: 提交普通评论"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  -d '{"body": "v5 API 测试评论（普通评论）- 测试后将被删除"}')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    COMMENT_ID=$(echo "$BODY" | jq -r '.id')
    echo "评论 ID: $COMMENT_ID"
    echo "✅ 普通评论创建成功"
    
    # 清理：删除测试评论
    DELETE_RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE \
      "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/comments/${COMMENT_ID}?access_token=$TOKEN")
    
    DELETE_CODE=$(echo "$DELETE_RESPONSE" | tail -n1)
    if [ "$DELETE_CODE" = "200" ] || [ "$DELETE_CODE" = "204" ]; then
        echo "✅ 测试评论已删除"
    else
        echo "⚠️ 删除失败，状态码: $DELETE_CODE"
    fi
else
    echo "❌ 创建失败，状态码: $HTTP_CODE"
    echo "响应: $BODY"
    exit 1
fi

# 测试 5: 提交单行行内评论（v5 API - 来自官方文档）
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 5: 提交单行行内评论"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  -d '{
    "body": "v5 API 单行行内评论测试 - 测试后将被删除",
    "path": "'"$FIRST_FILE"'",
    "position": 70
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    COMMENT_ID=$(echo "$BODY" | jq -r '.id')
    echo "评论 ID: $COMMENT_ID"
    echo "文件: $FIRST_FILE"
    echo "行号: 70"
    echo "✅ 单行行内评论创建成功"
    
    # 清理：删除测试评论
    DELETE_RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE \
      "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/comments/${COMMENT_ID}?access_token=$TOKEN")
    
    DELETE_CODE=$(echo "$DELETE_RESPONSE" | tail -n1)
    if [ "$DELETE_CODE" = "200" ] || [ "$DELETE_CODE" = "204" ]; then
        echo "✅ 测试评论已删除"
    else
        echo "⚠️ 删除失败，状态码: $DELETE_CODE"
    fi
else
    echo "❌ 创建失败，状态码: $HTTP_CODE"
    echo "响应: $BODY"
    exit 1
fi

# 测试总结
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试总结"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ 所有基本 v5 API 功能测试通过"
echo ""
echo "测试内容："
echo "  1. 获取 PR 信息 ✅"
echo "  2. 获取 PR 文件变更 ✅"
echo "  3. 获取 PR 评论列表 ✅"
echo "  4. 提交普通评论 ✅"
echo "  5. 提交单行行内评论 ✅"
echo ""
echo "注意：复杂的多行评论功能需要使用 test_inline_comment.sh 测试"