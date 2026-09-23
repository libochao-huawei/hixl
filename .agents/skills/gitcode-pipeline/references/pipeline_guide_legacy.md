# Legacy Pipeline 模式工作流程指导

> **适用条件**：`gp-detect.sh` 探测结果为 `mode=legacy`（仓库使用 v5 pipeline 接口族）。
> 通用步骤（1 检查 Label / 3 触发 / 6 分析框架 / 7 修复重试）见 SKILL.md。
> 接口细节（endpoint、参数、响应示例、`pipeline_detail` 用法）按需读取 `references/pipeline_api_legacy.md`。

## 步骤 2：查询流水线状态（仅当无 ci-pipeline-passed 时执行）

```bash
bash scripts/gp-list.sh <PR_NUMBER>
```

**返回格式**（key=value，每行一条流水线）：
```
id=526718 status=success sha=659baaa82e1a ref=fix/add-missing-semicolon-in-file-constant created=2026-04-30T16:08:16 pipeline_id=c85338dd... pipeline_run_id=159d8739... pipeline_detail={"hook_id":"42205",...}
```

**判断逻辑**：
- **退出码 3 / stderr 出现 `no_record`** → v5 接口对该 PR 返回 0 条。**这不等于没有流水线**，按顺序排查：
  ```bash
  bash scripts/gp-detect.sh <PR_NUMBER>      # 1) 仓库是否已迁 v8 Actions → 改用 gpv8-* 脚本族
  bash scripts/gp-comments.sh <PR_NUMBER>    # 2) Legacy 由外部系统上报时，从 PR 评论读 job 状态表
  bash scripts/_gp_pr_meta.sh <PR_NUMBER>    # 3) 门禁 label 与审批进度
  ```
  实测 Ascend/torchair PR#3725：v5 在 `type=report_pipeline` / `pipeline` / `all` / 无 type 四种参数下
  全部 `total=0`，而 AtlasAccount 一直在评论里贴 `#4120/#4122/#4123` 的完整 job 状态表，
  其中 #4123 全绿并最终把门禁 label 置为 `ci-pipeline-passed`。
  只有确认三条都查不到，才进入步骤 3 触发。
- `status=running` → 进入步骤 4 轮询
- `status=failed` 或 `status=canceled` → 进入步骤 5 分析失败
  （`canceled` 且各 job 全 ✅ 时，先怀疑是被 `concurrency.preemption` 抢占取消的，见 SKILL.md 步骤 3）
- `status=success` 但无 `ci-pipeline-passed` label → 流水线结果是旧 SHA 的，需要对比 SHA 确认：
  ```bash
  # 获取 PR 当前 HEAD SHA
  bash scripts/_gp_pr_meta.sh <PR_NUMBER> head_sha
  # 对比流水线 SHA（从 gp-list.sh 输出的 sha 字段取前 12 位）
  # 如果 SHA 不一致 → 流水线结果已过期，进入步骤 3 重新触发
  # 如果 SHA 一致 → label 可能被手动移除，报告异常
  ```

**关键字段（必须完整保存，后续接口都要用）**：
- `pipeline_id`: 详情接口路径参数
- `pipeline_run_id`: 详情接口 body 参数
- `pipeline_detail`: **必须完整保存**，后续所有接口调用的 body 参数

## 步骤 3 补充：API retry（Legacy 专属）

偶发环境失败且无代码变更时，优先精准重跑而非评论触发：

```bash
bash scripts/gp-api-retry.sh <PR_NUMBER>
```

- 与评论触发的区别：评论 `compile` 触发全新流水线运行（产生新记录）；API `retry` 重试指定流水线，更精准
- retry API 返回失败时，用 `gp-trigger.sh` 兜底

## 步骤 4：循环查询流水线状态

**默认用门禁轮询**（覆盖两套 CI，终态判据是门禁 label）：

```bash
bash scripts/gp-gate-wait.sh <PR_NUMBER> [超时分钟]   # timeout: 3600000ms (60分钟)
```

退出码：`0=passed 且无硬阻塞且审批满足`、`1=gate failed`、`2=CI 过了但仍有硬阻塞或审批不足`、`3=超时`。
终态会输出门禁面板（gate / PR 状态 / head_sha / 审批 / 硬阻塞 / 提示 / Legacy 报告 / v8 run）。

只有 `gp-detect.sh` 确认是**单系统 Legacy 仓库**时，才退而用：

```bash
bash scripts/gp-wait.sh <PR_NUMBER>  # timeout: 3600000ms (60分钟)
```

自动轮询直到状态为 `success`、`failed` 或 `canceled`。
`gp-list.sh` 返回 no_record 时它会打印排查提示并继续重试（旧版会在赋值处被 `set -e` 静默打死）。
终态为失败时会交叉校验门禁 label，若 label 不是 failed 则打 ⚠️ 提示两套 CI 结论不一致。

**轮询完成后反馈**：
- 成功 → 贴门禁面板，并明确 blocking / approval 是否也满足（CI 通过 ≠ 可合入）
- 失败 → `❌ 门禁 gate=failed，正在分析...`，进入步骤 5

## 步骤 5：失败处理 — 分析失败 Job

### 5.1 查询主流水线详情

从步骤 2 的输出中提取 `pipeline_id`、`pipeline_run_id`、`pipeline_detail`：

```bash
bash scripts/gp-detail.sh <pipeline_id> <pipeline_run_id> '<pipeline_detail>'
```

**返回格式**：
```
pipeline_name=cann_ge_all status=FAILED
[stage] 获取pr文件: COMPLETED
[stage] 子流水线: FAILED
[job]   compile: FAILED id=427f909686304e13be33f2ffb9fa07e1 task=official_devcloud_subPipeline step_id=20357724a95b48aa98b5f3934c833cfe
[job]   llt: FAILED id=0aa38cc75b8246f9907f7d7b69dd7ca1 task=official_devcloud_subPipeline step_id=0aa38cc75b8246f9907f7d7b69dd7ca1
```

**逻辑**：
- 只关注 `FAILED` 状态的阶段和 Job
- **所有 FAILED 的 Job 都必须逐个分析，禁止跳过任何一个**
- 同一阶段内可能有多个并行 Job（如 compile 和 llt），每个都需要独立分析失败原因
- 只有确认某个 Job 的失败是另一个 Job 失败的级联结果时（如子流水线 detail 返回 null/status=null），才可在报告中标注为级联失败，但仍需给出证据
- 如果 Job 的 `task=official_devcloud_subPipeline`，需要进入 5.2 获取子流水线信息
- 其他 task 类型（如 `official_devcloud_cloudBuild`），直接用输出中的 `id=` 字段进入 5.3 获取日志

> **禁止绕过脚本直接调用 curl 构造嵌套 JSON 请求**。`pipeline_detail` 包含嵌套 JSON，在 shell 中拼接会导致引号转义错误。所有需要调 API 的场景都已封装为 `scripts/` 下的脚本，必须通过脚本调用。

### 5.2 获取子流水线信息

```bash
bash scripts/gp-sub-output.sh <pipeline_id> <pipeline_run_id> '<pipeline_detail>' <step_id>
```

**返回格式**：
```
sub_pipeline_id=dcd161850837402293f0c47cda6b9921 sub_pipeline_run_id=c3d9c3663189481ca8d812c3046a4d95
```

然后用返回的 `sub_pipeline_id`、`sub_pipeline_run_id` 加上**原始的** `pipeline_detail`，调用 `gp-detail.sh` 获取子流水线的失败 Job 列表：

```bash
bash scripts/gp-detail.sh <sub_pipeline_id> <sub_pipeline_run_id> '<pipeline_detail>'
```

### 5.3 获取失败 Job 的日志

从步骤 5.1/5.2 的 `gp-detail.sh` 输出中提取失败 Job 的 `id` 字段：

```bash
JOB_ID=$(bash scripts/gp-detail.sh <pipeline_id> <pipeline_run_id> '<pipeline_detail>' | grep "FAILED" | grep -oP 'id=\K[^\s]+' | head -1)
```

然后传入 `gp-log.sh`：

```bash
bash scripts/gp-log.sh <pipeline_id> <pipeline_run_id> <job_id> '<pipeline_detail>' [lines]
```

**返回格式**（包含 error/fail/fatal 的日志行，默认最多 20 行）：
```
[2026/04/30 14:39:05] file_constant_kernel.cc:41:15: error: expected ';' at end of member declaration
[2026/04/30 14:39:05] make[2]: *** [...] Error 1
[2026/04/30 14:39:05] Failed command: make all -j8
```

**注意**：
- 日志默认倒序获取最后 500 行，然后 grep 错误行
- `pipeline_id`/`pipeline_run_id` 在子流水线场景下使用子流水线的值
- `job_id` 来自 `gp-detail.sh` 输出中失败 Job 行的 `id=` 字段

## 6.1 Legacy 模式的全量日志（用例失败定位）

用例执行失败的 gtest 详细输出通常在日志中间，必须用 `gp-log-full.sh` 获取全量日志后搜索（subagent A 任务）：

```bash
# subagent A：获取全量日志
bash scripts/gp-log-full.sh <pipeline_id> <pipeline_run_id> <job_id> '<pipeline_detail>'
# 日志保存到 pipeline_logs/<job_id>_full.log
grep '\[  FAILED  \]' pipeline_logs/<job_id>_full.log
```

## 6.2 覆盖率不足 — 定位未覆盖代码行（仅 Legacy）

当确认失败原因为覆盖率不足时，**必须使用 `gp-cov.sh` 脚本获取覆盖率报告**：

```bash
bash scripts/gp-cov.sh <pipeline_id> <pipeline_run_id> <job_id> '<pipeline_detail>'
```

**查找未覆盖行**：

```bash
# 在 HTML 覆盖率报告中搜索未覆盖标记
grep 'tlaUNC' pipeline_cov/<job_id>_cov/inc_cov/result/<test_type>/<file>.gcov.html
```

脚本会输出解压后的目录路径，解压后的目录结构：
```
pipeline_cov/<job_id>_cov/
├── inc_cov/
│   ├── diff_file@<源文件路径>    # PR新增代码的diff
│   ├── add_ut_<源文件>.info      # 增量覆盖率info文件
│   └── result/
│       └── <测试类型>/<源文件>.gcov.html  # HTML覆盖率报告
```

**覆盖率目标**：
- UT 测试：增量覆盖率需 ≥ 90%
- ST 测试：增量覆盖率需 ≥ 80%

**修复流程**：
1. 一定要先获取哪个源文件哪些行未覆盖，再决定增加ut/st用例，不要盲目增加用例．
2. 编写测试用例覆盖该行（参考现有测试文件风格）
3. 本地编译并运行验证（使用 `ge-dt-runner` skill）
4. push 代码 → 触发 CI

## Legacy 专属实践经验

> 以下经验来自实际盯 CI 过程中的踩坑总结，执行时务必遵守。

### 1. curl body 用文件传递，避免 shell 转义问题

`pipeline_detail` 中包含嵌套 JSON，在 shell 中用变量拼接（`$PIPELINE_DETAIL`）会被二次转义导致参数错误（PARAMETER_ERROR）。

**正确做法**：将完整 JSON 写入临时文件，用 `--data-binary @file` 传递。
```bash
cat > /tmp/body.json << 'ENDJSON'
{
    "pipeline_run_id": "xxx",
    "pipeline_detail": "{\"hook_id\":\"42205\", ...}"
}
ENDJSON

curl -s --request POST "https://api.gitcode.com/api/v5/repos/..." \
  --header 'Content-Type: application/json' \
  --data-binary @/tmp/body.json
```

**注意**：heredoc 使用 `'ENDJSON'`（带引号）防止 shell 变量展开。如果 body 中需要引用变量，先写文件再用 `sed` 替换。

### 2. 日志定位用倒序获取

编译日志通常 10MB+（`end_offset` 达到 1000 万+），正序逐页翻阅效率极低。错误信息都在日志末尾。

**正确做法**：倒序获取最后一页（`sort: desc`，`limit: 500`），然后 `grep` 关键词（`gp-log.sh` 已封装 desc 模式）。

### 3. 子流水线层级关系要严格遵守

`official_devcloud_subPipeline` 类型的 step 不能直接拿 job id 查日志，必须：
1. 调步骤输出接口（5.2）获取 `sub_pipeline_id` 和 `sub_pipeline_run_id`
2. 用子 pipeline 信息调详情接口（5.1）获取子流水线中的失败 job
3. 用子流水线的 `pipeline_id`、`pipeline_run_id`、`job_id` 查日志

**常见错误**：拿父流水线的 job_id 去查子流水线的日志，会返回 PARAMETER_ERROR。

### 4. 轮询用 list API，detail API 仅在需要时调用

| 场景 | 使用接口 | 原因 |
|------|----------|------|
| 轮询状态 | list API（`merge_requests/{id}/pipeline`） | 响应小、速度快 |
| 查看具体 job 失败原因 | detail API（`pipelines/{id}/pipeline-runs/detail`） | 需要 stages/jobs 详情 |
| 获取日志 | log API（`.../jobs/{job_id}/logs`） | 需要具体日志内容 |

### 5. 日志 API 翻页方法

日志 API 支持循环翻页获取全量日志，采用游标式分页。

翻页原理：
- 第 1 页：`start_offset="0", end_offset="0"` → API 自动确定窗口并返回日志
- 后续页：用响应返回的 `start_offset` 和 `end_offset` 作为下次请求的参数
- API 返回新的 `start_offset`、`end_offset` 和对应日志片段
- 当 `has_more=false` 时停止

编译错误通常在日志末尾，用 `gp-log.sh`（desc 模式）即可定位。**用例执行失败**的 gtest 详细输出通常在日志中间，必须用 `gp-log-full.sh` 获取全量日志后搜索。
