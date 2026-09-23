# v8 Actions API 参考（Actions 模式）

> 适用 `mode=actions` 的仓库（已接入 `.gitcode/workflows`，如 cann/ge）。工作流程见 `pipeline_guide_actions.md`。
>
> **认证方式**: `Authorization: Bearer $GITCODE_API_TOKEN`（与 v5 的 `access_token` query 参数等价）
> **Base URL**: `https://api.gitcode.com/api/v8/repos/{owner}/{repo}`

## 接口总览

| 端点 | 方法 | 用途 |
|------|------|------|
| `/actions/workflows` | GET | workflow 列表（发现 workflow_id；模式探测信号） |
| `/actions/runs?page=1&per_page=100` | GET | run 列表（含 `pull_request_id`，按 PR 过滤） |
| `/actions/runs/{run_id}` | GET | run 详情：stages[].jobs[].steps[] |
| `/actions/runs/{run_id}/jobs` | GET | jobs+steps 平铺明细 |
| `/actions/runs/{run_id}/jobs/{job_id}/download_log` | GET | job 日志 zip（**多层嵌套**） |

## A1. workflow 列表（模式探测信号）

```
curl -s "https://api.gitcode.com/api/v8/repos/{owner}/{repo}/actions/workflows" \
  -H "Authorization: Bearer $GITCODE_API_TOKEN"
```

**响应示例**（cann/ge 实测）：
```json
{
  "total_count": 6,
  "workflows": [
    {
      "workflow_id": "c8be889900de4f9386b12a317141a4eb",
      "file_path": ".gitcode/workflows/PR-pipeline_ge.yml",
      "name": "PR-pipeline_ge",
      "state": "active"
    }
  ]
}
```

**判断**: `total_count > 0` 或 `workflows` 非空 → 仓库为 Actions 模式。

## A2. run 列表（按 PR 过滤）

```
curl -s "https://api.gitcode.com/api/v8/repos/{owner}/{repo}/actions/runs?page=1&per_page=100" \
  -H "Authorization: Bearer $GITCODE_API_TOKEN"
```

**响应示例**（cann/ge 实测，截取单条）：
```json
{
  "total_count": 3459,
  "workflow_runs": [
    {
      "workflow_run_id": "5a3f1fbf8970408d9300ac332bc04ee5",
      "workflow_id": "c8be889900de4f9386b12a317141a4eb",
      "workflow_name": "PR-pipeline_ge",
      "file_path": ".gitcode/workflows/PR-pipeline_ge.yml",
      "status": "RUNNING",
      "event": "Note",
      "run_number": 2360,
      "head_branch": "test-add-empty-line-cmakelists",
      "head_sha": "24e7f43ef48e...",
      "pull_request_id": "4991",
      "actor": { "login": "stevenaw0", "name": "黄桂军" },
      "start_time": 1789541185000
    }
  ]
}
```

**关键字段**:
| 字段 | 说明 |
|------|------|
| `workflow_run_id` | run 唯一 ID（32 hex），后续所有接口的 `{run_id}` |
| `pull_request_id` | **PR 编号（字符串）**，按 PR 过滤的唯一可靠字段 |
| `head_sha` | **合并预览 SHA**，与 PR `head.sha` 不相等属正常，禁止用 SHA 相等判断归属 |
| `status` | `RUNNING` / `COMPLETED`(成功) / `FAILED` / `CANCELED` / `ABORTED` |
| `event` | 触发事件；评论 `compile` 触发的 run 为 `Note` |
| `start_time` | epoch **毫秒** |
| `run_number` | 仓库内自增序号 |

**过滤方法**: `jq -r --arg pr "4991" '[.workflow_runs[] | select(.pull_request_id == $pr)] | sort_by(.start_time) | reverse | .[0]'`（活跃仓库需翻页，最多扫 3 页通常足够；`gpv8-list.sh` 已封装）

## A3. run 详情（stages/jobs/steps）

```
curl -s "https://api.gitcode.com/api/v8/repos/{owner}/{repo}/actions/runs/{run_id}" \
  -H "Authorization: Bearer $GITCODE_API_TOKEN"
```

**响应结构**（截取，cann/ge PR #4991 run #2360 实测）：
```json
{
  "workflow_run_id": "5a3f1fbf8970408d9300ac332bc04ee5",
  "workflow_name": "PR-pipeline_ge",
  "status": "FAILED",
  "run_number": 2360,
  "head_branch": "test-add-empty-line-cmakelists",
  "pull_request_id": "4991",
  "start_time": 1789541184000,
  "stages": [
    {
      "id": "98f0d20f373d4078b41ed711fbb36612",
      "name": "PreBuild",
      "status": "COMPLETED",
      "start_time": 1789541193000,
      "end_time": 1789541237000,
      "jobs": [
        {
          "id": "87fcebe1331545a9b8b5500b62a82ee1",
          "name": "Test_image",
          "status": "COMPLETED",
          "start_time": 1789541215000,
          "end_time": 1789541220000,
          "steps": [
            {
              "id": "c82fe74dca3a438fa52cda69390c9a6c",
              "job_run_id": "87fcebe1331545a9b8b5500b62a82ee1",
              "name": "初始化步骤",
              "status": "COMPLETED",
              "sequence": 0
            }
          ]
        }
      ]
    }
  ]
}
```

**结构解读**:
- `stages[]`: 阶段，阶段间串行（`fail_fast` 前置失败会跳过后续阶段，状态为 `INIT`）
- `stages[].jobs[]`: 阶段内并发任务；**成对结构**：一个「展示 job」（如 `UT_Test_ge_common`，无 steps）+ 一个「执行 job」（如 `ut`，带 steps），失败 step 挂在执行 job 上
- `stages[].jobs[].steps[]`: 步骤明细，`sequence` 为序号，状态含 `IGNORED`（前置失败被跳过）
- 时间戳均为 epoch 毫秒

## A4. jobs + steps 平铺明细

```
curl -s "https://api.gitcode.com/api/v8/repos/{owner}/{repo}/actions/runs/{run_id}/jobs" \
  -H "Authorization: Bearer $GITCODE_API_TOKEN"
```

**响应**: `{ "total_count": 98, "jobs": [ { "id", "name", "status", "steps": [...] } ] }`，字段同 A3 的 jobs/steps，平铺无 stage 层级。适合需要完整步骤列表（含已完成）的场景。

## A5. 下载 job 日志（多层嵌套 zip）

```
curl -sL "https://api.gitcode.com/api/v8/repos/{owner}/{repo}/actions/runs/{run_id}/jobs/{job_id}/download_log" \
  -H "Authorization: Bearer $GITCODE_API_TOKEN" -o job_log.zip
```

**入参**: `{job_id}` 取「执行 job」的 `jobs[].id`（如 `ut` job 的 id，非 `UT_Test_ge_common` 展示 job 的 id）

**zip 结构（实测，共 2~3 层嵌套）**:
```
job_log.zip                                    # 第 1 层：download_log 返回
├── 0_<初始化步骤>.log                          # 第 2 层：每个 step 一个文件（中文步骤名有编码乱码）
├── 1_Checkout.log                              #   纯文本
├── 3_redis-cache.log                           #   纯文本
├── 4_ut_acc.log                                #   ⚠ 本身还是 zip！（测试类大日志步骤）
│   └── <execid>_<execid>.zip                   # 第 3 层
│       └── <execid>-<execid>-4.log             # 纯文本全量日志（可 grep [  FAILED  ]）
├── 5_UT_Cov.log                                # 1 字节（IGNORED step）
└── 6_upload.log                                 # 1 字节（IGNORED step）
```

**正确解压方法**（`gpv8-log.sh` 已内置，禁止手工只解一层）:
1. 解压第 1 层 zip
2. 对每个文件检查 magic（`file xxx.log` 或 `head -c2` 是否为 `PK`），是 zip 的继续解压（**必须用 find 递归扫描，嵌套 zip 解压后落在子目录里，顶层 glob 扫不到**）
3. 递归直到全部是纯文本
4. 全量日志已在本地，直接 grep，**无需翻页 API**

**错误摘要提取**（编译失败/用例失败通吃）:
```bash
grep -hiE "error|fatal|\[  FAILED  \]|\*\*\*Failed|tests? (passed|failed)" <log文件> | tail -30
```

## 触发与重试

- **触发**: 评论 `compile`（`gp-trigger.sh`），Actions run 的 `event` 字段记录为 `Note`
- **重试**: Actions 模式暂无验证过的 API retry，统一用评论触发（`gp-trigger.sh`）
