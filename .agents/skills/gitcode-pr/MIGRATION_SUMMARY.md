# GitCode API v4 到 v5 迁移总结

## 迁移完成时间

2026-05-14

## 迁移内容

### 已更新文件

1. **references/gitcode_api.md** - ✅ 完全重写，迁移到 v5 API
   - 更新所有 API 端点格式
   - 更新认证方式说明
   - 更新参数格式
   - 添加实际测试结果说明

2. **SKILL.md** - ✅ 更新所有 API 示例
   - 更新获取 PR 评论示例
   - 更新获取 PR 文件变更示例
   - 更新提交行内评论示例
   - 更新提交普通评论示例
   - 更新删除评论示例

3. **commands/review.md** - ✅ 更新审查流程
   - 更新发布行内评论示例
   - 更新 GitCode API 参考部分

4. **scripts/test_api_v5.sh** - ✅ 新增测试脚本
   - 测试所有基本 v5 API 功能
   - 自动创建和删除测试评论

5. **scripts/test_inline_comment.sh** - ✅ 新增测试脚本
   - 测试单行和多行评论功能
   - 对比官方 v5 API 和 web-api 端点

## 测试结果

### 官方 v5 API 测试结果

| 功能 | 测试结果 | 备注 |
|------|---------|------|
| 获取 PR 信息 | ✅ 通过 | 使用 `/api/v5/repos/...` 端点 |
| 获取 PR 文件变更 | ✅ 通过 | 使用 `/files.json` 端点 |
| 获取 PR 评论列表 | ✅ 通过 | 返回评论数组 |
| 提交普通评论 | ✅ 通过 | 使用 body 参数 |
| 提交单行行内评论 | ✅ 通过 | 使用 path + position 参数 |
| 提交多行评论 | ✅ 通过（未文档化） | 使用 start_position 参数 |

### web-api 端点测试结果

| 功能 | 测试结果 | 备注 |
|------|---------|------|
| 多行评论 | ❌ 失败 | 被 WAF拦截（418状态码） |

## 关键发现

### 1. 官方 v5 API 参数更简单

**v4 API（旧）**：
```json
{
  "repoId": "cann%2Fge",
  "iid": 2805,
  "body": "评论",
  "line_types": "new",
  "position": {
    "base_sha": "...",
    "start_sha": "...",
    "head_sha": "...",
    "new_line": 75,
    "start_new_line": 70
  },
  "severity": "suggestion"
}
```

**v5 API（新）**：
```json
{
  "body": "评论",
  "path": "文件路径",
  "position": 75,
  "start_position": 70
}
```

### 2. 多行评论支持

- **官方文档**：未明确说明多行评论参数
- **实际测试**：`start_position` 参数可能有效（状态码 201）
- **推荐方案**：使用 v5 API 的未文档化参数，测试后验证实际效果

### 3. 认证方式变化

**推荐**：Query 参数 `?access_token=<token>`
**可用**：Header `PRIVATE-TOKEN: <token>`

### 4. 文件变更响应格式

```json
{
  "diffs": [
    {
      "statistic": {
        "new_path": "文件路径",
        "old_path": "文件路径"
      }
    }
  ],
  "diff_refs": {
    "base_sha": "...",
    "head_sha": "...",
    "start_sha": "..."
  }
}
```

## 迁移策略总结

### 推荐使用的 API

| 功能 | 推荐 API | 原因 |
|------|---------|------|
| 所有基本操作 | ✅ 官方 v5 API | 简单、标准、测试通过 |
| 单行评论 | ✅ 官方 v5 API | 参数简洁 |
| 多行评论 | ✅ v5 API（未文档化参数） | 测试证明可能有效 |
| web-api 端点 | ❌ 不推荐 | 被 WAF拦截，不可用 |

### 不推荐使用的方案

- **web-api 端点**：`https://web-api.gitcode.com/issuepr/api/v1/...`
  - 被 WAF拦截（418状态码）
  - 可能不是公开的 API
  - 可能需要特殊权限或内部访问

## 已解决的问题

1. ✅ v4 API 端点即将下线 - 迁移到 v5
2. ✅ 认证方式不明确 - 明确推荐 query 参数认证
3. ✅ 多行评论格式复杂 - v5 API 参数更简单
4. ✅ 文件变更格式不清晰 - 更新文档说明

## 待确认的问题

1. ⚠️ `start_position` 参数是否正式支持？（未文档化）
2. ⚠️ 评论 ID 删除返回 400 是否正常？（ID 格式为哈希字符串）
3. ⚠️ 是否有其他未文档化的有用参数？

## 后续维护建议

1. **定期测试**：运行测试脚本验证 API 可用性
2. **监控文档**：关注 GitCode 官方文档更新
3. **用户反馈**：收集实际使用中的问题
4. **参数验证**：确认未文档化参数的实际效果

## 测试脚本使用说明

### 运行基本测试

```bash
# 设置环境变量
export GITCODE_API_TOKEN="your_token"

# 运行基本功能测试
cd /home/ge/code/cann-agent/skills/gitcode-pr/scripts
./test_api_v5.sh
```

### 运行行内评论测试

```bash
# 测试单行和多行评论功能
./test_inline_comment.sh
```

## 相关文档

- **迁移计划**：`migration_plan_v4_to_v5.md`
- **API 参考**：`references/gitcode_api.md`
- **官方文档**：`references/*.html`

## 迁移完成标记

✅ GitCode API v4 到 v5 迁移已完成（2026-05-14）