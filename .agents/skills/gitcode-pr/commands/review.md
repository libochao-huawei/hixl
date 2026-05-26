---
description: 审查 GitCode Pull Request 的代码变更
allowed-tools: Bash(curl), Bash(git diff), Bash(git log), Bash(git show)
---

# GitCode PR 代码审查

对指定的 Pull Request 进行代码审查。

**Agent 假设（适用于所有 agent 和子 agent）：**
- 所有工具都能正常工作，不会出错。不要测试工具或进行探索性调用。确保每个启动的子 agent 都清楚这一点。
- 只有在完成任务需要时才调用工具。每次工具调用都应有明确目的。

## 审查流程

请严格按照以下步骤执行：

### 步骤 1: 前置检查

检查以下条件是否成立：
- PR 已关闭
- PR 是草稿状态
- PR 不需要代码审查（如：自动化 PR、明显正确的微小变更）
- Claude 已经在此 PR 上评论过（通过 GitCode API 检查 PR 评论）

如果以上任一条件成立，停止执行，不继续后续步骤。

**注意**：仍然需要审查 Claude 生成的 PR。

### 步骤 2: 获取项目规范上下文

返回所有相关规范文件的文件路径列表（不含内容）：
- 根目录的规范文件（如 CONTRIBUTING.md、CODE_STYLE.md 等）
- PR 修改文件所在目录中的规范文件

### 步骤 3: 获取 PR 变更摘要

查看 PR 并返回变更摘要：

```bash
# 获取 PR 信息
curl -s "https://gitcode.com/api/v5/repos/{owner}/{repo}/pulls/{pr_number}" \
  -H "Authorization: Bearer $GITCODE_TOKEN"

# 获取 PR 变更文件列表
curl -s "https://gitcode.com/api/v5/repos/{owner}/{repo}/pulls/{pr_number}/files" \
  -H "Authorization: Bearer $GITCODE_TOKEN"

# 获取 PR diff
curl -s "https://gitcode.com/api/v5/repos/{owner}/{repo}/pulls/{pr_number}" \
  -H "Authorization: Bearer $GITCODE_TOKEN" \
  -d "diff=true"
```

### 步骤 3.1: 确定问题代码的准确行号

发布行内评论需要准确的行号。通过以下方法确定：

#### 方法 1: 从 PR Diff 获取

```bash
# 获取 PR 变更文件列表（包含 diff 信息）
curl -s "https://gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/files" \
  -H "Authorization: Bearer $GITCODE_API_TOKEN"
```

返回的 `patch.diff` 字段包含 hunk header，格式为：
```
@@ -old_start,old_count +new_start,new_count @@
```

- `-old_start,old_count`: 原文件的起始行和变更行数
- `+new_start,new_count`: 新文件的起始行和新增行数

**注意**：hunk header 只提供了起始行号，需要根据 diff 内容逐行计算实际行号。

#### 方法 2: 从 Raw 文件验证（推荐）

直接查询 PR 分支的 raw 文件获取准确行号：

```bash
# 获取 PR head commit 的 raw 文件
curl -s "https://raw.gitcode.com/${owner}/${repo}/raw/<HEAD_SHA>/path/to/file.cc" | grep -n "问题代码模式"
```

**示例**：
```bash
# 查找 if (it == 的行号
curl -s "https://raw.gitcode.com/${owner}/${repo}/raw/358192edfc1809f6fe17b0da0b1b8efd9880a52f/hcom_graph_optimizer.cc" \
  | grep -n "if (it =="
```

输出：`844:      if (it == (itMap->second).end()) {`

#### 最佳实践

1. **先用 diff 定位大致范围**：通过 PR diff 找到变更的代码块
2. **再用 raw 文件确认精确行号**：避免手动计算出错
3. **发布评论前验证**：确保行号对应的是真正的问题代码

### 步骤 4: 代码审查

从以下方面全面审查变更，返回问题列表，每个问题包含描述和标记原因（如"规范合规性"、"Bug"）：

**规范合规性审查**
检查变更是否符合项目规范。注意：评估文件的规范合规性时，只考虑该文件路径或父目录中的规范文件。

**Bug 扫描**
扫描明显的 Bug。只关注 diff 本身，不读取额外上下文。只标记显著的 Bug；忽略细微问题和可能的误报。不要标记需要查看 diff 之外上下文才能验证的问题。

**代码问题检查**
查找引入代码中的问题。可能是安全问题、逻辑错误等。只查找变更代码范围内的问题。

**关键原则：只需要高信号问题。** 标记以下类型的问题：
- 代码无法编译或解析（语法错误、类型错误、缺少导入、未解析引用）
- 无论输入如何，代码肯定会产生错误结果（明显的逻辑错误）
- 明确、无歧义的规范违反，可以引用被违反的具体规则

**不要标记：**
- 代码风格或质量问题
- 依赖于特定输入或状态的潜在问题
- 主观建议或改进意见

**如果你不确定某个问题是否真实存在，不要标记它。** 误报会损害信任并浪费审查者时间。

此外，每个子 agent 都应被告知 PR 标题和描述。这有助于理解作者的意图。

### 步骤 5: 验证问题

对于步骤 4 发现的每个问题，进行验证。验证工作应获取 PR 标题和描述以及问题描述，确信地验证所述问题确实是真实问题。

例如，如果标记了"变量未定义"这样的问题，验证代码中确实如此。另一个例子是规范合规问题，验证被违反的规范规则确实适用于此文件且确实被违反。

### 步骤 6: 过滤问题

过滤掉步骤 5 中未验证通过的问题。这一步将得到我们的高信号问题列表。

### 步骤 7: 输出审查摘要

在终端输出审查结果摘要：
- 如果发现问题：列出每个问题及简要描述
- 如果未发现问题：输出"未发现问题。已检查 Bug 和规范合规性。"

如果 **未提供** `--comment` 参数，在此停止。不要发布任何 GitCode 评论。

如果 **提供了** `--comment` 参数且 **未发现问题**，使用 GitCode API 发布摘要评论并停止。

如果 **提供了** `--comment` 参数且 **发现问题**，继续步骤 8。

### 步骤 8: 准备评论列表

创建计划发布的所有评论列表。这仅供你自己确认对评论内容满意。不要在任何地方发布此列表。

### 步骤 9: 发布行内评论

使用 GitCode API 为每个问题发布行内评论。

#### 创建新的 Discussion（推荐）

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "评论内容",
    "path": "文件相对路径",
    "position": <结束行号>,
    "start_position": <起始行号>
  }'
```

**多行选择说明**：
- `start_position`: 选中的起始行号（多行选择时设置）
- `position`: 选中的结束行号
- 单行评论时，只设置 `position`

**参数说明**：

| 参数 | 说明 | 必需 |
|------|------|------|
| `body` | 评论内容 | ✅ |
| `path` | 文件相对路径 | ✅ |
| `position` | 结束行号 | ✅ |
| `start_position` | 起始行号（多行选择） | 多行时 |

**注意**：`start_position` 参数是未文档化的参数，但测试证明可能有效。

对于每个评论：
- 提供问题的简要描述
- 对于小型、自包含的修复，包含可提交的建议代码块
- 对于较大的修复（6+ 行、结构性变更或跨多个位置的变更），描述问题和建议修复方式，不包含建议代码块
- 永远不要发布可提交建议，除非提交该建议能完全修复问题。如果需要后续步骤，不要留下可提交建议。

**重要：每个唯一问题只发布一条行内评论。不要发布重复评论。**

## 误报列表

在步骤 4 和 5 评估问题时使用此列表（这些是误报，不要标记）：

- 已存在的问题
- 看起来是 Bug 但实际上是正确的代码
- 高级工程师不会标记的吹毛求疵
- Linter 会捕获的问题（不要运行 Linter 验证）
- 一般代码质量问题（如缺乏测试覆盖、一般安全问题），除非规范中明确要求
- 规范中提到但在代码中明确静默的问题（如通过 lint ignore 注释）

## 注意事项

- 使用 GitCode API 与 GitCode 交互（如获取 Pull Request、创建评论）。不要使用网页抓取。
- 开始前创建待办列表。
- 必须在行内评论中引用并链接每个问题（如引用规范文件时，包含指向它的链接）。
- 如果未发现问题且提供了 `--comment` 参数，发布以下格式的评论：

---

## 代码审查

未发现问题。已检查 Bug 和规范合规性。

---

- 在行内评论中链接代码时，严格遵循以下格式，否则 Markdown 预览无法正确渲染：
  `https://gitcode.com/{owner}/{repo}/blob/{full_git_sha}/path/to/file#L{start}-L{end}`
  - 需要完整的 git sha
  - 必须提供完整 sha。类似 `https://gitcode.com/owner/repo/blob/$(git rev-parse HEAD)/foo/bar` 的命令不起作用，因为评论会直接在 Markdown 中渲染
  - 仓库名必须与你正在代码审查的仓库匹配
  - 文件名后使用 # 号
  - 行范围格式为 L[start]-L[end]
  - 在你评论的行前后至少提供 1 行上下文（如评论第 5-6 行，应链接到 L4-L7）

## GitCode API v5 参考

**重要**：GitCode v5 API 采用 GitHub 风格，推荐使用 query 参数认证。

### 快速参考

| 操作 | API 端点 |
|------|---------|
| 获取 PR 信息 | `GET /repos/${owner}/${repo}/pulls/<PR_NUMBER>` |
| 获取 PR 变更 | `GET /repos/${owner}/${repo}/pulls/<PR_NUMBER>/files.json` |
| 获取 PR 评论 | `GET /repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments` |
| 发布普通评论 | `POST /repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments` |
| 发布行内评论 | `POST /repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments` |

### 基本格式

```bash
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN"
```

**认证方式**：
- 推荐：Query 参数 `?access_token=$GITCODE_API_TOKEN`
- 也可用：Header `PRIVATE-TOKEN: $GITCODE_API_TOKEN`

**完整 API 格式（包括多行行内评论）详见**：`../references/gitcode_api.md`

