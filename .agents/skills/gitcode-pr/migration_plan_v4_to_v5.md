# GitCode API v4 到 v5 迁移计划

## 一、迁移背景

GitCode API v4 即将下线，需要将所有 API 调用迁移到 v5 版本。

## 二、当前使用 v4 API 的功能列表

### 1. 获取 PR 讨论（包含行内评论）
- **当前 API**: `GET https://api.gitcode.com/api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/discussions`
- **文件位置**: 
  - `SKILL.md:128-130`
  - `references/gitcode_api.md:149-155`

### 2. 获取 PR 文件变更
- **当前 API**: `GET https://api.gitcode.com/api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/changes`
- **文件位置**: 
  - `SKILL.md:173-174`
  - `references/gitcode_api.md:316-322`

### 3. 提交行内评论（支持多行选择）
- **当前 API**: `POST https://api.gitcode.com/api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/discussions`
- **文件位置**: 
  - `SKILL.md:185-211`
  - `references/gitcode_api.md:173-198`
  - `commands/review.md:161-187`

### 4. 回复已有 Discussion
- **当前 API**: `POST https://api.gitcode.com/api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/discussions/<DISCUSSION_ID>/notes`
- **文件位置**: 
  - `SKILL.md:236-241`
  - `references/gitcode_api.md:225-242`

### 5. 提交普通评论
- **当前 API**: `POST https://api.gitcode.com/api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/notes`
- **文件位置**: 
  - `SKILL.md:247-254`

### 6. 获取评论列表
- **当前 API**: `GET https://api.gitcode.com/api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/notes`
- **文件位置**: 
  - `SKILL.md:264-266`

## 三、v5 API 新格式（基于官方 HTML 文档）

### 官方 API 文档来源

参考 `references/` 目录下的 HTML 文档：
- `提交pull request 评论 _ GitCode 帮助文档.html`
- `获取某个Pull Request的所有评论 _ GitCode 帮助文档.html`
- `pr提交的文件变更信息 _ GitCode 帮助文档.html`
- `回复Pull Request评论 _ GitCode 帮助文档.html`

### v5 API 端点格式（官方文档）

**v5 API 采用 GitHub 风格**：
- 基础 URL: `https://api.gitcode.com/api/v5`
- 路径格式: `/repos/:owner/:repo/pulls/:number/...`
- 认证方式（官方文档推荐）: Query 参数 `?access_token=<token>`
- 也可使用 Header: `PRIVATE-TOKEN: <token>` 或 `Authorization: Bearer <token>`

**官方 HTML 文档中的认证示例**：
```bash
# 官方推荐：使用 query 参数
curl -L 'https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments?access_token=YOUR_TOKEN'

# 也可以使用 Header
curl -L 'https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments' \
  -H 'PRIVATE-TOKEN: YOUR_TOKEN'
```

### 用户提供的 web-api 端点（特殊格式）

用户提供的示例使用的是特殊的 web-api 端点：
- URL: `https://web-api.gitcode.com/issuepr/api/v1/projects/cann%2Fge/merge_requests/2805/discussions`
- 请求体包含复杂的 position 对象和额外字段（repoId、iid、assignee_id、proposer_id、severity）

**重要发现**：这个 web-api 端点不是标准的 v5 API，可能是一个特殊的内部 API 或扩展 API。

## 四、详细迁移步骤

### 重要发现：官方 v5 API vs web-api 端点

根据官方 HTML API 文档分析，发现关键差异：

**官方 v5 API（来自 HTML 文档）**：
- 端点：`https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments`
- 参数非常简单：`body`（必填）、`path`（可选）、`position`（可选）
- **不支持**复杂的 position 对象
- **不包含** base_sha、start_sha、head_sha、line_types 等字段
- **不支持**多行选择（文档中未提及 start_line 参数）

**web-api 端点（用户提供）**：
- 端点：`https://web-api.gitcode.com/issuepr/api/v1/projects/:repoId/merge_requests/:iid/discussions`
- 参数复杂：包含完整的 position 对象、repoId、iid、assignee_id、proposer_id、severity
- **支持**多行选择（有 start_new_line 参数）
- 采用 GitLab 风格的参数结构

**迁移策略建议**：
1. **普通评论和单行评论**：使用官方 v5 API（简单且标准）
2. **多行评论和复杂评论**：测试 web-api 端点是否可用，如果可用则继续使用
3. **需要确认**：web-api 端点的认证方式、稳定性和官方支持情况

### 步骤 1: 文档更新和准备（第 1 天）

#### 1.1 创建 API 迁移测试脚本
创建 `scripts/test_api_v5.sh` 用于测试所有 v5 API 端点（基于官方 HTML 文档）：

```bash
#!/bin/bash
# GitCode API v5 测试脚本（基于官方文档）

# 配置
REPO_OWNER="cann"
REPO_NAME="ge"
PR_NUMBER="2805"
TOKEN="$GITCODE_API_TOKEN"

echo "=== 测试 GitCode API v5 ==="
echo "仓库: ${REPO_OWNER}/${REPO_NAME}"
echo "PR: #${PR_NUMBER}"
echo ""

# 测试 1: 获取 PR 信息（v5 API）
echo "1. 获取 PR 信息..."
curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}?access_token=$TOKEN" \
  | jq '{number, title, state, base_sha: .base.sha, head_sha: .head.sha}'

# 测试 2: 获取 PR 文件变更（v5 API - 来自官方文档）
echo -e "\n2. 获取 PR 文件变更（files.json）..."
curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/files.json?access_token=$TOKEN" \
  | jq '.[0] | {filename, status, additions, deletions}'

# 测试 3: 获取 PR 评论列表（v5 API - 来自官方文档）
echo -e "\n3. 获取 PR 评论列表..."
curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  | jq 'length'

# 测试 4: 提交普通评论（v5 API - 来自官方文档）
echo -e "\n4. 提交普通评论..."
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  -d '{"body": "v5 API 测试评论 - 请忽略"}')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  COMMENT_ID=$(echo "$BODY" | jq -r '.id')
  echo "✅ 普通评论创建成功，ID: $COMMENT_ID"
  # 删除测试评论
  curl -s -X DELETE "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/comments/${COMMENT_ID}?access_token=$TOKEN"
  echo "✅ 测试评论已删除"
else
  echo "❌ 创建失败，状态码: $HTTP_CODE"
fi

echo -e "\n=== 测试完成 ==="
```

#### 1.2 创建行内评论测试脚本
创建 `scripts/test_inline_comment.sh` 专门测试最复杂的行内评论功能（基于官方文档）：

```bash
#!/bin/bash
# 测试 v5 API 提交行内评论（基于官方文档）

REPO_OWNER="cann"
REPO_NAME="ge"
PR_NUMBER="2805"
TOKEN="$GITCODE_API_TOKEN"

# 获取 PR 信息
PR_INFO=$(curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}?access_token=$TOKEN")

BASE_SHA=$(echo "$PR_INFO" | jq -r '.base.sha')
HEAD_SHA=$(echo "$PR_INFO" | jq -r '.head.sha')

echo "Base SHA: $BASE_SHA"
echo "Head SHA: $HEAD_SHA"

# 获取变更文件列表（使用官方的 files.json 端点）
FILES=$(curl -s "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/files.json?access_token=$TOKEN")

FIRST_FILE=$(echo "$FILES" | jq -r '.[0].filename')
echo "第一个变更文件: $FIRST_FILE"

# 测试 1: 使用官方 v5 API 提交单行行内评论
echo -e "\n测试 1: 官方 v5 API 单行评论..."
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  -d '{
    "body": "v5 API 单行测试评论 - 请忽略",
    "path": "'"$FIRST_FILE"'",
    "position": 70
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "HTTP 状态码: $HTTP_CODE"
if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  COMMENT_ID=$(echo "$BODY" | jq -r '.id')
  echo "✅ 单行评论成功，ID: $COMMENT_ID"
  curl -s -X DELETE "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/comments/${COMMENT_ID}?access_token=$TOKEN"
  echo "✅ 测试评论已删除"
else
  echo "❌ 单行评论失败"
  echo "响应: $BODY"
fi

# 测试 2: 尝试多行选择（未文档化参数）
echo -e "\n测试 2: 尝试多行选择..."
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/${PR_NUMBER}/comments?access_token=$TOKEN" \
  -d '{
    "body": "v5 API 多行测试（第 70-75 行）- 请忽略",
    "path": "'"$FIRST_FILE"'",
    "position": 75,
    "start_position": 70
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "HTTP 状态码: $HTTP_CODE"
if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  COMMENT_ID=$(echo "$BODY" | jq -r '.id')
  echo "✅ 多行评论可能支持（未文档化参数有效）"
  curl -s -X DELETE "https://api.gitcode.com/api/v5/repos/${REPO_OWNER}/${REPO_NAME}/pulls/comments/${COMMENT_ID}?access_token=$TOKEN"
else
  echo "❌ 官方 v5 API 不支持多行选择（需要使用 web-api 端点）"
fi

# 测试 3: 使用 web-api 端点（用户提供的特殊 API）
echo -e "\n测试 3: web-api 端点（用户提供的特殊格式）..."
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "PRIVATE-TOKEN: $TOKEN" \
  -H "Content-Type: application/json" \
  "https://web-api.gitcode.com/issuepr/api/v1/projects/cann%2Fge/merge_requests/2805/discussions" \
  -d '{
    "repoId": "cann%2Fge",
    "iid": 2805,
    "body": "web-api 端点测试（支持多行）- 请忽略",
    "line_types": "new",
    "position": {
      "base_sha": "'"$BASE_SHA"'",
      "start_sha": "'"$BASE_SHA"'",
      "head_sha": "'"$HEAD_SHA"'",
      "position_type": "text",
      "new_path": "'"$FIRST_FILE"'",
      "old_path": "'"$FIRST_FILE"'",
      "new_line": 75,
      "start_new_line": 70,
      "old_line": -1,
      "ignore_whitespace_change": false
    },
    "severity": "suggestion"
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "HTTP 状态码: $HTTP_CODE"
if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  echo "✅ web-api 端点可用且支持多行选择"
else
  echo "❌ web-api 端点不可用或需要特殊认证"
fi

echo -e "\n=== 测试总结 ==="
echo "官方 v5 API: 简单格式，支持单行评论"
echo "web-api 端点: 复杂格式，支持多行选择"
echo "建议: 简单评论用 v5，复杂评论用 web-api"
```

### 步骤 2: 功能迁移（第 2-3 天）

#### 2.1 更新 references/gitcode_api.md

**需要修改的部分**:

1. **获取 PR 讨论**（第 149-155 行）
   - 旧: `GET /api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/discussions`
   - 新: `GET /api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/discussions`
   
   ```bash
   # v5 API
   curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
     "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/discussions"
   ```

2. **获取 PR 文件变更**（第 316-322 行）
   - 旧: `GET /api/v4/projects/${encoded_repo}/merge_requests/<PR_NUMBER>/changes`
   - 新: `GET /api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/files`
   
   ```bash
   # v5 API
   curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
     "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/files"
   ```

3. **提交行内评论**（最复杂，第 169-198 行）
   
   **官方 v5 API 格式（来自 HTML 文档）**：
   ```bash
   POST https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments
   
   Body 参数：
   - body (string, required): 评论内容
   - path (string, optional): 文件的相对路径
   - position (integer, optional): 代码所在行数
   
   示例：
   curl -L 'https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments' \
     -H 'Content-Type: application/json' \
     -H 'Accept: application/json' \
     -d '{
       "body": "这是一个测试评论",
       "path": "runtime/v1/opskernel_executor/ops_kernel_executor_manager.cc",
       "position": 70
     }'
   ```
   
   **关键差异**：
   - v5 API 使用简单的 `path` 和 `position` 参数，不需要复杂的 position 对象
   - v5 API 不需要 base_sha、start_sha、head_sha
   - v5 API 不需要 line_types 字段
   - v5 API 不需要 assignee_id、proposer_id、severity 字段
   
   **注意**：用户提供的 web-api 端点（`https://web-api.gitcode.com/issuepr/api/v1/...`）可能是用于更复杂的多行选择评论，需要单独测试验证

4. **回复已有 Discussion**（第 225-242 行）
   
   **官方 v5 API 格式（来自 HTML 文档）**：
   ```bash
   POST https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/discussions/:discussion_id/comments
   
   Path 参数：
   - owner (string, required): 仓库所属空间地址
   - repo (string, required): 仓库路径
   - number (integer, required): PR序数
   - discussion_id (string, required): 讨论ID
   
   Body 参数：
   - body (string, required): 评论内容
   
   示例：
   curl -L 'https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/discussions/171372926/comments' \
     -H 'Content-Type: application/json' \
     -H 'Accept: application/json' \
     -d '{
       "body": "回复内容"
     }'
   ```

5. **提交普通评论**（第 246-257 行）
   
   **官方 v5 API 格式**（同提交行内评论，但不提供 path 和 position）：
   ```bash
   POST https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments
   
   Body 参数：
   - body (string, required): 评论内容
   - path (string, optional): 文件路径（普通评论不提供）
   - position (integer, optional): 行号（普通评论不提供）
   
   示例：
   curl -L 'https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments' \
     -H 'Content-Type: application/json' \
     -H 'Accept: application/json' \
     -d '{
       "body": "这是一个普通评论"
     }'
   ```

6. **获取评论列表**（第 264-266 行）
   
   **官方 v5 API 格式（来自 HTML 文档）**：
   ```bash
   GET https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments
   
   Path 参数：
   - owner (string, required): 仓库所属空间地址
   - repo (string, required): 仓库路径
   - number (integer, required): PR序数
   
   Query 参数：
   - access_token (string, required): 用户授权码
   
   示例：
   curl -s 'https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments?access_token=$GITCODE_API_TOKEN'
   ```

#### 2.2 更新 SKILL.md

需要修改的位置：
- 第 128-136 行：获取 PR 讨论
- 第 173-178 行：获取 PR 文件变更
- 第 185-211 行：提交行内评论
- 第 236-241 行：回复已有 Discussion
- 第 247-254 行：提交普通评论
- 第 264-270 行：获取评论 ID

#### 2.3 更新 commands/review.md

需要修改的位置：
- 第 41-53 行：获取 PR 信息和变更
- 第 161-187 行：发布行内评论
- 第 256-270 行：GitCode API 参考

#### 2.4 检查 scripts/create_pr.py

该文件已使用 v5 API（第 60、94 行），无需修改。

### 步骤 3: 全面测试（第 4-5 天）

#### 3.1 测试 PR: https://gitcode.com/cann/ge/pull/2805

使用此 PR 进行所有功能的集成测试。

#### 3.2 功能测试清单

##### 测试 1: 获取 PR 信息
```bash
# 测试命令
curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805" | jq '{number, title, state, base: .base.sha, head: .head.sha}'

# 预期结果
# 返回 PR 的基本信息，包括 base.sha 和 head.sha
```

##### 测试 2: 获取 PR 讨论（包含行内评论）
```bash
# 测试命令
curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/discussions" | jq '.[0]'

# 预期结果
# 返回讨论列表，每个讨论包含:
# - id: 讨论ID
# - notes: 评论数组
# - notes[].body: 评论内容
# - notes[].position: 行内评论位置信息
```

##### 测试 3: 获取 PR 文件变更
```bash
# 测试命令
curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/files" | jq '.[0] | {filename, status, additions, deletions}'

# 预期结果
# 返回变更文件列表，每个文件包含:
# - filename: 文件路径
# - status: 变更状态（added/modified/removed）
# - additions: 新增行数
# - deletions: 删除行数
```

##### 测试 4: 获取 PR 评论列表
```bash
# 测试命令
curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments" | jq '.[0] | {id, body, path, position}'

# 预期结果
# 返回评论列表
```

##### 测试 5: 提交普通评论（最简单）
```bash
# 测试命令
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments" \
  -d '{
    "body": "这是一个 v5 API 测试评论 - 请忽略"
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "HTTP 状态码: $HTTP_CODE"
echo "响应体: $BODY"

# 验证
if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  COMMENT_ID=$(echo "$BODY" | jq -r '.id')
  echo "✅ 测试通过 - 评论已创建，ID: $COMMENT_ID"
  
  # 清理测试数据
  curl -s -X DELETE \
    "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/comments/$COMMENT_ID?access_token=$GITCODE_API_TOKEN"
  echo "✅ 测试评论已删除"
else
  echo "❌ 测试失败"
fi
```

##### 测试 6: 提交行内评论（最复杂，重点测试）

**准备工作**：
```bash
# 1. 获取 PR 信息（v5 API）
PR_INFO=$(curl -s "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805?access_token=$GITCODE_API_TOKEN")

BASE_SHA=$(echo "$PR_INFO" | jq -r '.base.sha')
HEAD_SHA=$(echo "$PR_INFO" | jq -r '.head.sha')

echo "Base SHA: $BASE_SHA"
echo "Head SHA: $HEAD_SHA"

# 2. 获取变更文件列表（v5 API）
FILES=$(curl -s "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/files.json?access_token=$GITCODE_API_TOKEN")

FIRST_FILE=$(echo "$FILES" | jq -r '.[0].filename')
echo "第一个变更文件: $FIRST_FILE"
```

**测试 6.1: 使用官方 v5 API 提交行内评论（推荐）**
```bash
# 官方 v5 API 格式（来自 HTML 文档）
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "v5 API 行内评论测试（官方格式）- 请忽略",
    "path": "'"$FIRST_FILE"'",
    "position": 70
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "官方 v5 API 测试结果:"
echo "HTTP 状态码: $HTTP_CODE"
echo "响应体: $BODY"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  COMMENT_ID=$(echo "$BODY" | jq -r '.id')
  echo "✅ 测试通过 - 评论已创建，ID: $COMMENT_ID"
  
  # 清理测试数据
  curl -s -X DELETE \
    "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/comments/$COMMENT_ID?access_token=$GITCODE_API_TOKEN"
  echo "✅ 测试评论已删除"
else
  echo "❌ 测试失败"
  echo "尝试查看错误详情..."
fi
```

**测试 6.2: 测试用户提供的 web-api 端点（特殊格式）**
```bash
# 用户提供的特殊 web-api 端点
# 注意：这个端点可能不是标准 v5 API，而是特殊的扩展 API
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  "https://web-api.gitcode.com/issuepr/api/v1/projects/cann%2Fge/merge_requests/2805/discussions" \
  -d '{
    "repoId": "cann%2Fge",
    "iid": 2805,
    "body": "web-api 端点测试 - 请忽略",
    "line_types": "new",
    "position": {
      "base_sha": "'"$BASE_SHA"'",
      "start_sha": "'"$BASE_SHA"'",
      "head_sha": "'"$HEAD_SHA"'",
      "position_type": "text",
      "new_path": "'"$FIRST_FILE"'",
      "old_path": "'"$FIRST_FILE"'",
      "new_line": 70,
      "old_line": -1,
      "ignore_whitespace_change": false
    },
    "severity": "suggestion"
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "web-api 端点测试结果:"
echo "HTTP 状态码: $HTTP_CODE"
echo "响应体: $BODY"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  echo "✅ web-api 端点可用"
else
  echo "❌ web-api 端点不可用或需要特殊认证"
fi
```

**测试 6.3: 对比两种 API 的功能差异**
```bash
# 问题：官方 v5 API 的参数格式非常简单，不支持多行选择
# 需要确认：
# 1. 官方 v5 API 是否支持多行选择？如果支持，参数格式是什么？
# 2. web-api 端点是否是 v5 的替代方案，用于处理复杂评论？
# 3. 是否有其他 v5 API 端点用于多行评论？

echo "需要进一步测试的问题："
echo "1. v5 API 是否支持 start_line 参数（用于多行选择）？"
echo "2. v5 API 的 position 参数是否可以是一个对象？"
echo "3. 是否有专门的 v5 API 端点用于创建 discussions？"
```

##### 测试 7: 回复已有 Discussion
```bash
# 1. 先获取一个现有的 discussion
DISCUSSION_ID=$(curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/discussions" | jq -r '.[0].id')

echo "Discussion ID: $DISCUSSION_ID"

# 2. 回复这个 discussion
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/discussions/$DISCUSSION_ID/comments" \
  -d '{
    "body": "v5 API 回复测试 - 请忽略"
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "回复测试结果:"
echo "HTTP 状态码: $HTTP_CODE"
echo "响应体: $BODY"
```

##### 测试 8: 删除评论
```bash
# 1. 先创建一个测试评论
CREATE_RESPONSE=$(curl -s -X POST \
  -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments" \
  -d '{"body": "测试删除功能 - 将被删除"}')

COMMENT_ID=$(echo "$CREATE_RESPONSE" | jq -r '.id')
echo "创建的测试评论 ID: $COMMENT_ID"

# 2. 删除评论
DELETE_RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/comments/$COMMENT_ID?access_token=$GITCODE_API_TOKEN")

HTTP_CODE=$(echo "$DELETE_RESPONSE" | tail -n1)

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "204" ]; then
  echo "✅ 删除评论成功"
else
  echo "❌ 删除评论失败"
fi
```

#### 3.3 多行选择行内评论测试

**问题分析**：
官方 v5 API 文档中的参数非常简单（只支持 `path` 和 `position`），没有提到多行选择的参数（如 `start_line`）。
这导致以下几种可能：

1. **可能情况 1**: v5 API 不支持多行选择，需要使用特殊的 web-api 端点
2. **可能情况 2**: v5 API 支持但文档未记录，需要尝试未文档化的参数
3. **可能情况 3**: v5 API 有其他端点用于多行评论

测试多行选择功能（这是最复杂的）：

```bash
# 测试 1: 尝试在官方 v5 API 中添加 start_line 参数（未文档化）
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "多行选择测试（第 70-75 行，尝试未文档化参数）- 请忽略",
    "path": "'"$FIRST_FILE"'",
    "position": 75,
    "start_line": 70,
    "start_position": 70
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "未文档化参数测试结果:"
echo "HTTP 状态码: $HTTP_CODE"
echo "响应体: $BODY"

# 测试 2: 使用用户提供的 web-api 端点（明确支持多行）
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  "https://web-api.gitcode.com/issuepr/api/v1/projects/cann%2Fge/merge_requests/2805/discussions" \
  -d '{
    "repoId": "cann%2Fge",
    "iid": 2805,
    "body": "多行选择测试（web-api 端点，第 70-75 行）- 请忽略",
    "line_types": "new",
    "position": {
      "base_sha": "'"$BASE_SHA"'",
      "start_sha": "'"$BASE_SHA"'",
      "head_sha": "'"$HEAD_SHA"'",
      "position_type": "text",
      "new_path": "'"$FIRST_FILE"'",
      "old_path": "'"$FIRST_FILE"'",
      "new_line": 75,
      "start_new_line": 70,
      "old_line": -1,
      "ignore_whitespace_change": false
    },
    "severity": "suggestion"
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "web-api 多行选择测试结果:"
echo "HTTP 状态码: $HTTP_CODE"
echo "响应体: $BODY"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
  echo "✅ web-api 端点支持多行选择"
else
  echo "❌ web-api 端点不支持多行选择或认证失败"
fi

# 测试 3: 尝试查找 v5 API 中是否有专门的 discussions 端点
# 检查是否存在: POST /api/v5/repos/:owner/:repo/pulls/:number/discussions
echo -e "\n尝试查找 v5 discussions 端点..."
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/discussions?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "测试 v5 discussions 端点 - 请忽略",
    "path": "'"$FIRST_FILE"'",
    "position": {
      "base_sha": "'"$BASE_SHA"'",
      "start_sha": "'"$BASE_SHA"'",
      "head_sha": "'"$HEAD_SHA"'",
      "new_line": 70,
      "start_new_line": 70
    }
  }')

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "v5 discussions 端点测试:"
echo "HTTP 状态码: $HTTP_CODE"
if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ] || [ "$HTTP_CODE" = "404" ]; then
  if [ "$HTTP_CODE" = "404" ]; then
    echo "⚠️ 端点不存在"
  else
    echo "✅ 端点存在且可用"
  fi
else
  echo "⚠️ 端点可能存在但需要不同的参数格式"
fi
```

**多行选择测试结论**：
根据测试结果，需要确定：
- 如果 web-api 端点可用且支持多行：继续使用 web-api 端点处理多行评论
- 如果官方 v5 API 有未文档化的参数支持多行：更新文档记录该参数
- 如果两者都不可用：需要寻找其他解决方案或联系 GitCode 技术支持

#### 3.4 错误处理测试

测试各种错误情况：

1. **无效的 PR 号**：
```bash
curl -s -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/999999"
```

2. **无效的文件路径**：
```bash
curl -s -X POST \
  -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments" \
  -d '{
    "body": "测试无效路径",
    "commit_id": "'"$HEAD_SHA"'",
    "path": "nonexistent/file.cc",
    "line": 10
  }'
```

3. **无效的行号**：
```bash
curl -s -X POST \
  -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments" \
  -d '{
    "body": "测试无效行号",
    "commit_id": "'"$HEAD_SHA"'",
    "path": "'"$FIRST_FILE"'",
    "line": 999999
  }'
```

4. **无权限删除他人评论**：
```bash
# 尝试删除不属于自己的评论
curl -s -X DELETE \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/comments/123456?access_token=$GITCODE_API_TOKEN"
```

### 步骤 4: 文档完善和培训（第 6 天）

#### 4.1 更新所有示例代码

确保所有文档中的示例都使用 v5 API。

#### 4.2 创建迁移日志

在 `references/migration_log.md` 中记录：
- 所有 API 端点的变更
- 响应格式的变化
- 已废弃的参数
- 新增的参数

#### 4.3 更新测试用例

如果有自动化测试，更新测试用例以使用 v5 API。

## 五、风险和缓解措施

### 风险 1: v5 API 与 v4 API 行为不一致

**缓解措施**：
- 在迁移前，使用 v5 API 测试所有功能
- 准备回滚计划，保留 v4 API 的代码分支
- 分阶段迁移，先迁移简单功能，再迁移复杂功能

### 风险 2: 多行选择行内评论格式变化

**缓解措施**：
- 这是功能最复杂的部分，需要重点测试
- 准备多种 API 格式的测试方案
- 与 GitCode 技术支持确认正确的 API 格式

### 风险 3: 认证方式变化

**缓解措施**：
- 测试两种认证方式：`Authorization: Bearer <token>` 和 `PRIVATE-TOKEN: <token>`
- 文档中明确说明推荐的认证方式

### 风险 4: API 限流变化

**缓解措施**：
- 测试 v5 API 的限流策略
- 在代码中添加适当的重试逻辑

## 六、验收标准

### 功能验收

**官方 v5 API（简单功能）**：
- [ ] 能够使用 v5 API 获取 PR 信息（`/api/v5/repos/...`）
- [ ] 能够使用 v5 API 获取 PR 文件变更（`/api/v5/.../files.json`）
- [ ] 能够使用 v5 API 获取 PR 评论列表（`/api/v5/.../comments`）
- [ ] 能够使用 v5 API 提交普通评论（body 参数）
- [ ] 能够使用 v5 API 提交单行行内评论（body + path + position）
- [ ] 能够使用 v5 API 回复 Discussion（`/api/v5/.../discussions/:id/comments`）
- [ ] 能够使用 v5 API 删除评论

**web-api 端点（复杂功能）**：
- [ ] 能够使用 web-api 提交多行行内评论（start_new_line 参数）
- [ ] web-api 认证方式正确
- [ ] web-api 返回格式正确处理

**认证方式**：
- [ ] v5 API query 参数认证（`?access_token=<token>`）工作正常
- [ ] v5 API Header 认证（`PRIVATE-TOKEN`）工作正常
- [ ] web-api 认证方式测试通过

**错误处理**：
- [ ] v5 API 404 错误正确处理（PR 不存在）
- [ ] v5 API 401/403 错误正确处理（认证失败）
- [ ] 参数错误返回正确的错误信息

### 文档验收

- [ ] 所有 API 示例标注使用哪种 API（v5 或 web-api）
- [ ] `references/gitcode_api.md` 明确区分两种 API 的适用场景
- [ ] 认证方式说明清晰（query 参数 vs Header）
- [ ] 参数说明准确（基于官方 HTML 文档）
- [ ] 示例代码可直接运行
- [ ] 多行评论使用 web-api 的说明清晰

### 性能验收

- [ ] v5 API 响应时间合理（< 5秒）
- [ ] web-api 响应时间合理（< 5秒）
- [ ] 无超时错误
- [ ] 无内存泄漏

### 测试脚本验收

- [ ] `scripts/test_api_v5.sh` 所有测试通过
- [ ] `scripts/test_inline_comment.sh` 所有测试通过
- [ ] 测试脚本能够自动清理测试数据

## 七、时间表

| 天数 | 任务 | 负责人 | 状态 |
|------|------|--------|------|
| 第 1 天 | 准备测试脚本、创建迁移文档 | - | ⏸️ 待开始 |
| 第 2 天 | 更新 references/gitcode_api.md | - | ⏸️ 待开始 |
| 第 3 天 | 更新 SKILL.md 和 commands/review.md | - | ⏸️ 待开始 |
| 第 4 天 | 全面功能测试（简单功能） | - | ⏸️ 待开始 |
| 第 5 天 | 重点测试行内评论（复杂功能） | - | ⏸️ 待开始 |
| 第 6 天 | 文档完善、培训、验收 | - | ⏸️ 待开始 |

## 八、后续维护

### 监控 v5 API 变化

- 订阅 GitCode API 更新公告
- 定期检查 API 文档更新
- 建立自动化测试监控 API 可用性

### 用户反馈

- 收集用户对迁移的反馈
- 记录并解决用户遇到的问题
- 更新 FAQ 文档

## 九、最终迁移决策

根据官方 HTML 文档分析和测试结果，制定以下迁移决策：

### API 选择策略

| 功能 | v4 API | v5 API（官方） | web-api（特殊） | 决策 |
|------|--------|---------------|----------------|------|
| 获取 PR 信息 | `/api/v4/projects/...` | `/api/v5/repos/...` | - | ✅ 使用 v5 |
| 获取 PR 文件变更 | `/api/v4/.../changes` | `/api/v5/.../files.json` | - | ✅ 使用 v5 |
| 获取 PR 评论 | `/api/v4/.../discussions` | `/api/v5/.../comments` | - | ✅ 使用 v5 |
| 提交普通评论 | `/api/v4/.../notes` | `/api/v5/.../comments` | - | ✅ 使用 v5（body 参数）|
| 提交单行评论 | `/api/v4/.../discussions`（复杂 position） | `/api/v5/.../comments`（简单） | `/web-api/.../discussions` | ✅ 使用 v5（path + position）|
| 提交多行评论 | `/api/v4/.../discussions`（start_new_line） | ❓ 不确定（无文档） | `/web-api/.../discussions`（明确支持） | ✅ 使用 web-api |
| 回复 Discussion | `/api/v4/.../notes` | `/api/v5/.../discussions/:id/comments` | - | ✅ 使用 v5 |

### 参数格式对比

**v5 API（官方文档）**：
```json
{
  "body": "评论内容",
  "path": "文件路径",
  "position": 70
}
```

**web-api 端点（用户示例）**：
```json
{
  "repoId": "cann%2Fge",
  "iid": 2805,
  "body": "评论内容",
  "line_types": "new",
  "position": {
    "base_sha": "...",
    "start_sha": "...",
    "head_sha": "...",
    "new_line": 75,
    "start_new_line": 70,
    ...
  },
  "severity": "suggestion"
}
```

### 迁移实施方案

1. **优先使用官方 v5 API**：
   - 所有简单的查询操作
   - 所有普通评论和单行评论
   - 回复 Discussion
   
2. **特殊场景使用 web-api**：
   - 多行选择的行内评论
   - 需要额外字段（severity、assignee_id）的评论
   - 需要复杂 position 对象的场景

3. **认证方式**：
   - v5 API：优先使用 query 参数 `?access_token=<token>`
   - v5 API 也可使用 Header `PRIVATE-TOKEN: <token>`
   - web-api：需要测试确认认证方式

4. **文档更新重点**：
   - 明确区分官方 v5 API 和 web-api 的适用场景
   - 在 `references/gitcode_api.md` 中添加"多行评论使用 web-api"的说明
   - 更新所有示例代码，标注使用哪种 API

### 待确认问题

在实施迁移前，需要通过测试确认：

1. ❓ web-api 端点是否是官方支持的长久方案？
2. ❓ web-api 的认证方式是什么？
3. ❓ v5 API 是否有未文档化的参数支持多行？
4. ❓ 如果 web-api 不可用，多行评论的替代方案是什么？

建议在迁移前先用测试脚本（`scripts/test_inline_comment.sh`）验证这些问题。

## 十、参考资源

### 官方 API 文档（HTML）

- `references/提交pull request 评论 _ GitCode 帮助文档.html` - v5 API 提交评论官方文档
- `references/获取某个Pull Request的所有评论 _ GitCode 帮助文档.html` - v5 API 获取评论官方文档
- `references/pr提交的文件变更信息 _ GitCode 帮助文档.html` - v5 API 文件变更官方文档
- `references/回复Pull Request评论 _ GitCode 帮助文档.html` - v5 API 回复评论官方文档

### 其他资源

- GitCode API v5 文档: https://docs.gitcode.com/docs/apis/
- 测试 PR: https://gitcode.com/cann/ge/pull/2805
- 用户提供的 web-api 示例: https://web-api.gitcode.com/issuepr/api/v1/projects/cann%2Fge/merge_requests/2805/discussions