# v8 Actions 模式工作流程指导

> **适用条件**：`gp-detect.sh` 探测结果为 `mode=actions`（仓库已接入 `.gitcode/workflows`，如 cann/ge）。
> 通用步骤（1 检查 Label / 3 触发 / 6 分析框架 / 7 修复重试）见 SKILL.md。
> 接口细节（endpoint、参数、响应示例）按需读取 `references/pipeline_api_actions.md`。

## 步骤 2：查询流水线状态（仅当无 ci-pipeline-passed 时执行）

```bash
bash scripts/gpv8-list.sh <PR_NUMBER>
```

**返回格式**（仅最新一条）：
```
run_id=5a3f1fbf8970408d9300ac332bc04ee5 run_number=2360 status=RUNNING sha=24e7f43ef48e ref=test-xxx workflow=PR-pipeline_ge event=Note created=2026-09-16T07:41:24Z
```

**判断逻辑**：
- 无输出 → 无运行记录，进入步骤 3 触发
- `status=RUNNING` → 进入步骤 4 轮询
- `status=FAILED/CANCELED/ABORTED` → 进入步骤 5 分析失败
- `status=COMPLETED` 但无 `ci-pipeline-passed` label → 确认该 run 是否为该 PR 的最新 run（`gpv8-list.sh` 已返回最新一条；**注意：v8 run 的 `head_sha` 是合并预览 SHA，与 PR `head.sha` 不相等属正常，禁止用 SHA 相等判断**）

## 步骤 4：循环查询流水线状态

**默认用门禁轮询，不要直接盯单个 v8 run**：

```bash
bash scripts/gp-gate-wait.sh <PR_NUMBER> [超时分钟]   # timeout: 3600000ms (60分钟)
```

- 终态判据是**门禁 label**（`ci-pipeline-passed` / `ci-pipeline-failed`），不是 v8 run status
- 每个间隔输出一行：`gate=... state=... | v8 run#N=... | legacy <名#N>=... | approval=...`
- 终态输出「门禁面板」：gate / PR 状态 / head_sha / 审批进度 / 硬阻塞 / 提示 / Legacy 报告 / v8 run
- 退出码：`0=passed 且无硬阻塞且审批满足`、`1=gate failed`、`2=CI 过了但仍有硬阻塞或审批不足`、`3=超时`
- v8 run 失败但 gate=passed 时，面板会打 ⚠️ 并提示按环境问题上报，**不要改代码**

只有 `gp-detect.sh` 确认是**单系统 Actions 仓库**时，才退而用：

```bash
bash scripts/gpv8-wait.sh <PR_NUMBER 或 RUN_ID>  # timeout: 3600000ms (60分钟)
```

- 传 PR 编号：自动解析最新 run 并轮询（运行记录出现前持续等待，最多 10 分钟）
- 传 run_id（32 位 hex）：直接轮询该 run
- 终态映射：`COMPLETED` → 成功（exit 0）；`FAILED/CANCELED/ABORTED` → 失败（exit 1）
- 传 PR 编号且终态为失败时，会自动交叉校验门禁 label；若 label 不是 failed，会打印 ⚠️ 提示两套 CI 结论不一致

**为什么不能只盯 v8 run**（实测 Ascend/torchair PR#3725）：v8 run #64/#66/#67 三次 FAILED 在
`sca-pr`/`malicious-scan`（报"当前扫描仓库不在openlibing中"，平台侧配置缺失、重试不可恢复），
而同一时间 Legacy 流水线 #4123 全 job ✅、顶层 ✅，门禁 label 置为 `ci-pipeline-passed`，PR 正常合入。
`gpv8-wait.sh` 09:31:54 就 `TERMINAL:FAILED` 退出了，label 09:44 才变绿。

**轮询完成后反馈**：
- 成功 → 贴门禁面板，并明确 blocking / approval 是否也满足
- 失败 → `❌ 门禁 gate=failed，正在分析...`，进入步骤 5

## 步骤 5：失败处理 — 分析失败 Job

### 5A.1 查询 run 详情

从步骤 2 的输出中提取 `run_id`：

```bash
bash scripts/gpv8-detail.sh <RUN_ID>
```

**返回格式**：
```
workflow_name=PR-pipeline_ge status=FAILED run_number=2360 sha=24e7f43ef48e branch=test-xxx
[stage] PreBuild: COMPLETED
[stage] LLT: FAILED
[job]   UT_Test_ge_common: FAILED id=53d06e26074d446e8b34c0ec117146f1
[job]   ut: FAILED id=1c837c66fc4d474b874be96b515104fb (failed_steps: ut_acc)
```

**逻辑**：
- 只关注 `FAILED`/`CANCELED` 状态的 stage 和 job，**所有失败 job 必须逐个分析，禁止跳过任何一个**
- **job 成对结构**：Actions 模式下同一任务常有一个「展示 job」（如 `UT_Test_ge_common`，无 steps、无日志）和一个「执行 job」（如 `ut`，带 steps），失败 step 挂在执行 job 上，**下载日志必须用执行 job 的 id**（展示 job 下载会得到空内容）
- 需要完整步骤列表（含已完成的）时用 `gpv8-jobs.sh <RUN_ID>`（加 `--all` 输出全部 job）：
  ```bash
  bash scripts/gpv8-jobs.sh <RUN_ID>          # 仅非 COMPLETED 的 job
  bash scripts/gpv8-jobs.sh <RUN_ID> --all    # 全部 job
  ```
- `INIT` 状态的 job 是前置失败的级联结果，可标注为级联失败，但需给出证据

### 5A.2 下载失败 Job 的日志

从 5A.1 输出中提取失败 job 的 `id` 字段（优先执行 job）：

```bash
bash scripts/gpv8-log.sh <RUN_ID> <JOB_ID>
```

**返回格式**（日志自动落盘到 `pipeline_logs/<job_id>/`，错误摘要分级输出）：
```
log_dir=/path/to/pipeline_logs/1c837c66fc4d474b874be96b515104fb
---- error summary ----
[gtest 失败用例]
[  FAILED  ] UtestGeApiV2.run_graph_with_stream
[CTest 汇总]
[2026-09-16 15:49:21.362+08:00]  1/14 Test  #6: ut_libge_multiparts_utest ................***Failed   80.64 sec
[2026-09-16 15:52:38.470+08:00] 93% tests passed, 1 tests failed out of 14
[其他错误行（最后 10 行，已过滤 GE 运行时 ERROR 日志）]
...
```

**注意**：
- `download_log` 返回的 zip 是**多层嵌套**（步骤 `.log` 本身还是 zip，内含 `<execid>_<execid>.zip`，再解压才是纯文本），`gpv8-log.sh` 已内置递归解压，禁止手工只解一层就 grep（会得到空结果）
- 全量日志已落本地文件（`log_dir` 下每个 step 一个 `.log`），后续 grep 定位 gtest 失败用例**无需翻页 API**
- 中文步骤名文件名有服务端编码乱码（如 `0_хИЭхзЛ...log`），按序号前缀对应 step 即可

## 6.1 Actions 模式的全量日志（用例失败定位）

Actions 模式下全量日志已由 `gpv8-log.sh` 落地本地（`log_dir`），**无需 subagent 翻页获取**，主 agent 直接 grep 即可（仅保留 subagent B 编译复现）：

```bash
grep '\[  FAILED  \]' pipeline_logs/<job_id>/*.log
# 提取断言详情（失败用例上下文）：
grep -B 20 '\[  FAILED  \]' pipeline_logs/<job_id>/*.log | head -40
```

全量日志文件位于 `pipeline_logs/<job_id>/<step名>.log__unpacked/...`（嵌套 zip 解压后的最终纯文本）。

## v8 Actions 专属实践经验

> 以下经验来自实际盯 CI 过程中的踩坑总结，执行时务必遵守。

### 1. v8 Actions 日志是多层嵌套 zip

`download_log` 返回的 zip 里，`<N>_<step>.log` 文件本身可能还是 zip（内含 `<execid>_<execid>.zip`，再解压才是纯文本日志），共 2~3 层嵌套。`gpv8-log.sh` 已内置递归解压；**禁止手工只解一层就 grep（会得到空结果）**。

### 2. v8 Actions 的 job 成对结构

同一测试任务常有一个「展示 job」（如 `UT_Test_ge_common`，无 steps）和一个「执行 job」（如 `ut`，steps 里有 `ut_acc` 等实际步骤）。失败 step 和日志都挂在**执行 job** 上；下载日志必须用执行 job 的 id，用展示 job 的 id 会得到空内容或无关日志。

### 3. v8 Actions 状态值与时间戳

- run 级：`RUNNING` → `COMPLETED`（**成功**，不是 SUCCESS）/ `FAILED` / `CANCELED` / `ABORTED`
- stage/job/step 级：`INIT` / `RUNNING` / `COMPLETED` / `FAILED` / `CANCELED` / `IGNORED`（前置失败被跳过）
- 所有时间戳均为 **epoch 毫秒**
- run 列表按 `pull_request_id` 字符串比较过滤（注意不是数字比较）

### 4. v8 run 的 head_sha 是合并预览 SHA

`actions/runs[].head_sha` 与 PR 的 `head.sha` 不相等属正常（实测 PR #4991 head 为 `ca17d27cf`，对应 run 的 head_sha 为 `24e7f43ef`）。判断 run 是否属于某 PR **必须用 `pull_request_id` 字符串比较**，判断是否最新用 `start_time` 最大值，**禁止用 SHA 相等判断**。
