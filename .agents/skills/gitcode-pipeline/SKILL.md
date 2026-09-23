---
name: gitcode-pipeline
description: 触发 GitCode PR 流水线，并循环查询流水线状态直到完成。支持双 CI 模式（v8 Actions 新接口 + v5 Legacy Pipeline 旧接口），自动识别仓库 CI 模式并按模式加载对应指导文档。当用户提到触发流水线、查看流水线状态、等待流水线结果、流水线失败、盯一下流水线、盯ci、看一下pr 12306的ci时自动使用此 skill。
---

# GitCode Pipeline Skill

## 执行原则

1. **先列计划**：执行前必须先向用户展示执行计划，然后再执行，计划制定后 MUST 直接执行，DO NOT 等待用户确认后再开始执行（展示计划≠请示批准）
2. **逐步反馈**：每完成一个步骤，向用户输出简要进度信息，不要闷头执行。
3. **日志落盘**：所有失败任务的日志必须保存到 `pipeline_logs` 目录，文件名包含 PR 编号、任务名和时间戳。

## 前置准备

### 提取仓库信息

```bash
repo_url=$(git remote get-url origin)
if [[ $repo_url == git@* ]]; then
  owner=$(echo $repo_url | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
  repo=$(echo $repo_url | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
else
  owner=$(echo $repo_url | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
  repo=$(echo $repo_url | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
fi
encoded_repo=$(printf '%s' "${owner}/${repo}" | jq -sRr @uri)
```

### 验证环境变量

检查 `GITCODE_API_TOKEN` 是否已设置，未设置则提示用户。

## 步骤 0：CI 模式识别（必做）

GitCode 存在**两套 CI 系统**，不同仓库使用不同系统，**不能按组织猜测**（实测 cann/ge 与 Ascend/pytorch 均已接入 Actions，部分仓库仍为 Legacy Pipeline）：

| 模式 | 接口前缀 | 脚本族 | 仓库特征 |
|------|----------|--------|----------|
| **Actions**（新） | `/api/v8/repos/{owner}/{repo}/actions/*` | `gpv8-*.sh` | 仓库含 `.gitcode/workflows/*.yml` |
| **Legacy**（旧） | `/api/v5/.../pipelines/*`（依赖 `pipeline_detail` 嵌套 JSON） | `gp-*.sh` | v5 pipeline 接口有运行记录 |

**探测信号（按优先级）**：

1. **PR 运行记录（最权威）**：v8 `actions/runs` 中存在 `pull_request_id == PR号` 的记录 → Actions；v5 `merge_requests/{pr}/pipeline` 返回 `content` 非空 → Legacy
2. **仓库 workflow 声明**（PR 无运行记录时）：v8 `actions/workflows` 返回非空 workflow 列表 → Actions
3. **robot 评论链接**（辅助校验）：cann-robot 评论含 `gitcode.com/{org}/{repo}/actions/runs/{run_id}` → Actions，且可直接提取 run_id
4. **仓库文件**（本地 clone 场景）：存在 `.gitcode/workflows/` 目录 → Actions

```bash
bash scripts/gp-detect.sh <PR_NUMBER>   # 提供 PR 号时按实际运行记录探测，最可靠
bash scripts/gp-detect.sh               # 无 PR 时按 workflow 声明探测
# 输出 mode=actions → 后续用 gpv8-*.sh；mode=legacy → 后续用 gp-*.sh
# 退出码: 0=actions, 1=legacy, 2=none
```

**决策规则**：
- 两套系统都有该 PR 记录时（过渡期并存），优先 Actions
- 判断 run 归属 PR 用 `pull_request_id`（字符串比较），判断新旧用 `start_time` 最大值

**触发方式通用**：评论 `compile`（`gp-trigger.sh`）两种模式均可触发（Actions 的 run 会记录 `event=Note`）。

### 模式分发（探测完成后必做）

**根据探测结果加载对应的模式指导文档，步骤 2/4/5 及失败分析按该文档执行**；需要接口细节时再按需读取对应 API 参考：

| 探测结果 | 加载指导文档（必读） | API 参考（按需） | 脚本族 |
|----------|----------------------|------------------|--------|
| `mode=actions` | `references/pipeline_guide_actions.md` | `references/pipeline_api_actions.md` | `gpv8-*.sh` |
| `mode=legacy` | `references/pipeline_guide_legacy.md` | `references/pipeline_api_legacy.md` | `gp-*.sh` |

本文档只保留两种模式**通用**的步骤（1/3/6/7）和通用经验。

## 执行计划模板

每次执行前，需要先列出计划然后执行

```
## 执行计划

| 步骤 | 操作 | 说明 |
|------|------|------|
| 0 | 识别 CI 模式 | `gp-detect.sh`，并按结果加载对应模式指导文档 |
| 1 | 检查门禁 | `_gp_pr_meta.sh <PR>`，看 gate / blocking / approval（通用，权威判据） |
| 2 | 查询流水线状态 | 仅当 gate 非 passed 时执行；按模式用 `gpv8-list.sh` / `gp-list.sh`；Legacy 报 no_record 时用 `gp-comments.sh` 兜底 |
| 3 | 触发流水线（如需要） | `gp-trigger.sh`（内置抢占保护，CI 在跑时 exit 2）；Legacy 偶发失败可优先 `gp-api-retry.sh` |
| 4 | 循环查询状态 | **默认 `gp-gate-wait.sh`**（以门禁 label 为终态，覆盖两套 CI）；仅单系统仓库才用 `gpv8-wait.sh` / `gp-wait.sh` |
| 5 | 处理结果 | 成功则输出门禁面板并检查其余阻塞项；失败则按模式指导文档获取日志并分析 |
| 6 | 修复 | 如果是编译失败，dt用例失败或覆盖率不足，需要尝试修改代码，push代码后重新触发ci |
```

**每完成一步，向用户反馈**，例如：
- `✅ 步骤 0 完成：mode=actions，已加载 pipeline_guide_actions.md`
- `✅ 步骤 1 完成：gate=running，需要轮询`
- `✅ 步骤 3 完成：已评论触发流水线，等待启动...`

## 脚本一览

| 脚本 | 适用模式 | 用途 | 超时要求 |
|------|----------|------|----------|
| `scripts/gp-detect.sh` | 通用 | 识别仓库 CI 模式（步骤 0 必做） | 默认 |
| `scripts/_gp_pr_meta.sh` | 通用 | 一次取齐门禁元信息：labels / gate / 硬阻塞 / 提示 / 审批进度 / Legacy 报告（内部 helper） | 默认 |
| `scripts/gp-gate-wait.sh` | 通用 | **盯 CI 首选**：以门禁 label 为终态轮询，终态输出门禁面板 | **60 分钟** |
| `scripts/gp-comments.sh` | 通用 | 从 PR 评论读机器人上报的流水线报告（Legacy 在 v5 接口查不到时的唯一途径） | 默认 |
| `scripts/gp-trigger.sh` | 通用 | 评论触发流水线（关键词 `compile`）；**内置抢占保护**，CI 正在跑时拒绝并 exit 2 | 默认 |
| `scripts/gpv8-list.sh` | Actions | 查 PR 的 run 列表（最新一条） | **20分钟** |
| `scripts/gpv8-detail.sh` | Actions | 查 run 详情（stage/job/step） | 默认 |
| `scripts/gpv8-jobs.sh` | Actions | 查 jobs+steps 明细 | 默认 |
| `scripts/gpv8-log.sh` | Actions | 下载 job 日志（自动递归解压多层 zip）+ 分级错误摘要 | 默认 |
| `scripts/gpv8-wait.sh` | Actions | 轮询单个 v8 run 状态直到终态（失败时会交叉校验门禁 label） | **60 分钟** |
| `scripts/gp-list.sh` | Legacy | 查流水线列表；v5 无记录时 exit 3 并输出排查路径 | **20分钟** |
| `scripts/gp-detail.sh` | Legacy | 查流水线详情(阶段/Job) | 默认 |
| `scripts/gp-sub-output.sh` | Legacy | 查子流水线步骤输出 | 默认 |
| `scripts/gp-log.sh` | Legacy | 查日志(末尾错误摘要) | 默认 |
| `scripts/gp-log-full.sh` | Legacy | 循环翻页获取全量日志 | 默认 |
| `scripts/gp-cov.sh` | Legacy | 获取覆盖率报告并解压 | 默认 |
| `scripts/gp-api-retry.sh` | Legacy | API retry 重跑指定流水线（需传 content id） | 默认 |
| `scripts/gp-analyze-failure.sh` | Legacy | 一键分析失败：自动穿透子流水线获取失败Job和日志 | 默认 |
| `scripts/gp-retry.sh` | Legacy | 评论触发CI + 自动轮询直到完成 | 默认 |
| `scripts/gp-wait.sh` | Legacy | 轮询单条 Legacy 流水线直到终态（失败时会交叉校验门禁 label） | **60 分钟** |

所有脚本兼容 Windows/Linux/Mac（依赖 bash + jq + curl + unzip）。

**日志落盘**：`gpv8-log.sh` / `gp-log-full.sh` / `gp-cov.sh` / `gp-analyze-failure.sh` 默认写到
`${TMPDIR:-/tmp}/gitcode_pipeline_logs/`，**不再写 cwd**（旧版写相对路径 `pipeline_logs/`，
而这些脚本又必须在仓库里执行才能自动探测 owner/repo，结果会在用户仓库里留下未跟踪目录，
一个 `git add -A` 就把 CI 日志提交进去了）。可用 `GP_LOG_DIR` 覆盖，`GP_ANALYZE_LOG_DIR` 为兼容别名。

**执行时必须使用 `scripts/` 目录下的封装脚本，不要直接调用 curl**，封装脚本会用 jq 提取关键字段，大幅减少返回体积和 token 消耗。

## 步骤 1：检查 PR Label（通用，CI 通过状态的权威判断）

通过 PR Label 判断 CI 是否真正通过。**流水线的 `status=success` 可能是旧 SHA 的结果，只有 `ci-pipeline-passed` label 才能证明最新代码已通过 CI。**

```bash
bash scripts/_gp_pr_meta.sh <PR_NUMBER>          # 一次取齐 labels/gate/阻塞/审批/Legacy 报告
bash scripts/_gp_pr_meta.sh <PR_NUMBER> gate     # 只要门禁结论: passed|failed|running|unknown
```

**判断逻辑**：
- `gate=passed` → **CI 已通过最新代码**。但 CI 通过 ≠ 可合入，必须同时看面板里的 `blocking`（硬阻塞 label）与 `approval`（lgtm/approve 进度）
- `gate=failed` → 进入步骤 5 分析失败
- `gate=running` → 进入步骤 4 轮询
- `gate=unknown`（无 `ci-pipeline-*` label）→ CI 可能尚未触发，进入步骤 2

**⚠️ smoke/docs 流水线不是真 CI（实测 Ascend/torchair PR#3746 踩坑）**：
`smoke-pipeline-running` / `smoke-pipeline-success`、`docs-ci-pipeline-success` 等 label，以及
AtlasAccount 上报的 `PR-pipeline_<repo>_smoke#N` 报告，都是 PR 创建/push 后**自动触发的辅助
流水线**（冒烟测试、文档 CI），不参与门禁，也**不代表真 CI 正在运行**。看到
`gate=unknown` + 仅 smoke 报告"运行中"时，正确结论是**真 CI 尚未触发**，应进入步骤 2/3
评论 `compile` 触发，而不是跳过触发去轮询。`_gp_pr_meta.sh` 的 `legacy_report` 已对 smoke
报告追加 `(smoke, 非真 CI)` 标注并优先输出真流水线报告。

**为什么必须检查 Label 而非仅看流水线 status**：
1. 当 PR 推送新代码后，旧的流水线 `status` 仍为 `success`（不会自动变为 failed），但该结果是旧 SHA 的。流水线通过后系统会自动添加 `ci-pipeline-passed` label，新代码推送后该 label 会被移除。
2. **双 CI 并存时，任一系统的 run status 都可能与门禁结论相反。** 实测 Ascend/torchair PR#3725：v8 Actions run #64/#66/#67 三次 FAILED（`sca-pr`/`malicious-scan` 报"当前扫描仓库不在openlibing中"，平台侧配置缺失），而同一时间 Legacy 流水线 #4123 全部 job ✅、顶层 ✅，门禁 label 置为 `ci-pipeline-passed`。只信 Actions 会去修一个根本不存在的代码问题。

## 步骤 2 补充：`gp-list.sh` 无输出 ≠ 没有流水线

Legacy 侧 `gp-list.sh` 现在在 v5 接口返回 0 条时以 **exit 3** 结束并输出排查路径（旧版静默返回空，导致 `gp-analyze-failure.sh` 拿着空字符串继续走并静默死掉，看起来像"这个 PR 还没触发 CI"）。

出现 `no_record` 时按顺序排查：

```bash
bash scripts/gp-detect.sh <PR_NUMBER>      # 1) 仓库是否已迁 v8 Actions
bash scripts/gpv8-list.sh <PR_NUMBER>      # 2) mode=actions → 走 gpv8-* 脚本族
bash scripts/gp-comments.sh <PR_NUMBER>    # 3) Legacy 由外部系统上报时，从 PR 评论读 job 状态表
```

第 3 条是这类仓库获取 Legacy 状态的**唯一**途径：实测 Ascend/torchair PR#3725 的 v5 接口在
`type=report_pipeline` / `pipeline` / `all` / 无 type 四种参数下全部 `total=0`，而 AtlasAccount
一直在评论里贴 `#4120/#4122/#4123` 的完整 job 状态表。

## 步骤 3：触发流水线（通用）

**⚠️ 触发前必须确认没有正在跑的流水线（抢占保护）。**

很多仓库的 workflow 配了：

```yaml
concurrency:
  preemption:
    enable: true
    events: [mr_id]
```

此时评论 `compile` 会**取消**同一 PR 上正在运行的流水线 —— 包括一个可能所有 job 都已通过、只差顶层收尾的 run。实测 Ascend/torchair PR#3725 因此连续损失两条全绿流水线（`#4120`、`#4122` 均为「全部 job ✅ COMPLETED、顶层 🟨 CANCELED」），白白多花约 20 分钟。

`gp-trigger.sh` 已内置该保护：触发前检查门禁 label（`ci-pipeline-running`）与 v8 run 状态，命中则拒绝并 `exit 2`；确需强制触发加 `--force`。之所以两个信号都要看：被抢占的往往是 Legacy 流水线，而 Legacy 在部分仓库上 v5 接口查不到，只有 label 能反映它。

**触发策略（优先 API retry）**：

| 场景 | 触发方式 | 原因 |
|------|----------|------|
| 偶发环境失败，无代码变更（步骤 6 判定） | `gp-api-retry.sh`（仅 Legacy） | 精准重跑，不产生多余记录 |
| 无流水线记录（步骤 2 确认 no_record） | `gp-trigger.sh` | 无可重试对象，必须评论触发 |
| 修复代码后 push（步骤 7） | `gp-trigger.sh` | SHA 已变，需跑新代码 |
| retry API 返回失败 | `gp-trigger.sh` | 兜底方案 |

**执行方式**：

```bash
# 方式 A：API retry（仅 Legacy 模式，优先）
bash scripts/gp-api-retry.sh <PR_NUMBER>

# 方式 B：评论触发（两种模式通用；Actions 模式唯一触发方式）
bash scripts/gp-trigger.sh <PR_NUMBER>            # CI 正在跑时会拒绝，exit 2
bash scripts/gp-trigger.sh <PR_NUMBER> --force    # 明确要抢占时才用
```

**退出码**：`0=已触发`、`1=用法/环境错或发送失败`、`2=拒绝（CI 正在运行，或 PR 已 merged/closed）`。

触发后等待 10-15 秒再查询（Actions 模式运行记录注册可能延迟 1~2 分钟）。

**重试前必须先比对错误指纹**：若上一次失败的同名 job 报的是**完全相同**的错误，且错误属于配置类
（如"项目不在 openlibing 中"、"仓库未注册"、权限/配额），那不是偶发，重试只会得到同样结果 ——
直接按步骤 7.1 的「环境/基础设施错误（已知问题）」上报用户后停止。

## 步骤 6：分析日志并报告（通用框架）

首先判断是**编译失败**还是**用例执行失败**：
- 编译失败 → 日志中包含编译错误（`.cc` 文件 + error 行号 + `make` 错误）
- 用例执行失败 → 日志中包含 `tests passed` / `tests failed` / `FAILED` 的 CTest 汇总

根据日志内容判断失败原因，进入步骤 7 处理：

**多 Job 失败时的报告格式**（每个 FAILED Job 必须单独列出）：
```
### 失败分析摘要
| Job | 失败原因 | 类型 | 是否需要修复 |
|-----|----------|------|-------------|
| compile | 第三方依赖下载 HTTP 429 | 环境偶发 | 否 |
| llt | 子流水线未启动（detail 返回 null） | compile 的级联失败 | 否（随 compile 重试自动恢复） |
```

| 错误类型 | 处理方式 | 必须调用的脚本 |
|----------|----------|----------------|
| 编译错误 | 报告具体文件和行号 → 步骤 7 修复代码后 push 并重触 CI | Actions: `gpv8-log.sh` / Legacy: `gp-log.sh` |
| UT/ST 执行错误 | 报告失败用例 → **进入 6.1** | Actions: `gpv8-log.sh` / Legacy: `gp-log-full.sh` |
| 覆盖率不足 | 报告覆盖率缺口 → **进入 6.2**（仅 Legacy，`gp-cov.sh`） | `gp-cov.sh` |
| 代码告警 | 评估是否误报 → 误报则报告停止，非误报则步骤 7 修复后 push 并重触 CI | Actions: `gpv8-log.sh` / Legacy: `gp-log.sh` |
| 环境/基础设施错误（已知） | 报告用户后停止 | - |
| 环境/基础设施错误（偶发） | 步骤 3 重试流水线（Legacy 用 `gp-api-retry.sh`） | `gp-api-retry.sh` |

**误报判断原则**：
- 已知的环境问题或工具链 bug
- 与本次修改无关的历史告警
- 静态分析工具的误判

如果判断为误报，立即停止修复尝试，向用户报告：

```
⚠️ 流水线失败，但经分析为误报：
   任务: static-check
   告警: xxx
   原因: 与本次修改无关/已知误报
   建议: 忽略或联系维护人员
```

### 6.1 用例执行失败 — 定位失败用例（通用纪律，严格顺序，禁止跳步）

当步骤 6 确认为用例执行失败时，**必须按以下顺序执行，禁止跳过任何步骤**：

**第 1 步：从日志提取 CTest 汇总**

用模式对应的日志脚本获取摘要，从中提取：
- 失败的 target 数量和名称（如 `ut_libge_multiparts_utest (Failed)`）
- 通过率（如 `92% tests passed, 1 tests failed out of 12`）
- exit code（exit code 1 = 断言失败，exit code 139 = segfault）

此时已经知道失败的是哪个二进制（如 `ut_libge_multiparts_utest`）。

**第 2 步：并行执行全量日志获取 + 失败二进制编译**

接下来**必须并行启动两个 subagent**，节省总耗时：

| subagent | 任务 | 说明 |
|----------|------|------|
| subagent A | 获取全量日志 | 按模式获取全量日志（见下），搜索 `[  FAILED  ]` 定位具体 gtest test case |
| subagent B | 编译失败二进制 | fetch PR 分支代码到本地 → 使用 `ge-dt-runner` skill 编译失败的二进制 target |

```bash
# subagent A（Legacy 模式）：翻页获取全量日志
bash scripts/gp-log-full.sh <pipeline_id> <pipeline_run_id> <job_id> '<pipeline_detail>'
grep '\[  FAILED  \]' pipeline_logs/<job_id>_full.log

# subagent A（Actions 模式）：全量日志已由 gpv8-log.sh 落地本地，无需 subagent，主 agent 直接：
grep '\[  FAILED  \]' pipeline_logs/<job_id>/*.log
# （Actions 模式可省略 subagent A，仅保留 subagent B 编译复现）
```

```bash
# subagent B：编译失败二进制（必须 fetch PR 分支，禁止使用已有编译产物）
# 1. 先 git fetch PR 分支（参考步骤 7.2）
# 2. 使用 ge-dt-runner skill 编译失败的二进制 target（如 ut_libge_multiparts_utest）
# 3. 禁止偷懒直接使用旧二进制，必须确保代码与线上 PR 一致
```

**禁止偷懒直接使用旧编译产物**：
- 旧二进制可能存在 protobuf 注册冲突、符号未定义、架构不匹配等问题
- 必须先 fetch PR 分支最新代码，再使用 `ge-dt-runner` skill 编译
- 只编译失败的 target，不要全量编译

**第 3 步：主 agent 协调 — 并发等待与即时反馈**

两个 subagent 并发期间，主 agent **必须**：
- 向用户报告两个任务的进度状态（如"全量日志获取中（已下载 X MB）... 编译进行中..."）
- **禁止**闷头等待不反馈

当其中一个 subagent 先完成时，主 agent **必须立即处理已完成的结果，不等另一个**：

| 谁先完成 | 主 agent 立即执行 |
|----------|-------------------|
| subagent A（日志）完成 | 立即从全量日志中搜索 `[  FAILED  ]` 定位失败用例，提取断言详情（`grep -B 20`），向用户报告具体失败用例名、文件、行号、期望值/实际值 |
| subagent B（编译）完成 | 向用户报告编译结果（成功/失败），如编译失败则报告错误 |

当两个 subagent 都完成后：
- 从全量日志拿到失败用例名
- 编译产物的二进制 + `--gtest_filter=<失败用例名>` 运行本地验证
- 进入步骤 7 修复并重试

## 步骤 7：修复并重试流水线（通用）

> **直接按下方表格执行，DO NOT 向用户确认。** 表格已覆盖所有场景，没有歧义。

**重触发 CI 的前置条件（必须满足全部）：**
- [ ] 已定位到具体失败的用例名或编译错误位置
- [ ] 已分析根因（编译错误/断言失败/crash/环境问题/偶发）
- [ ] 如果是代码问题：已修复并 push
- [ ] 如果是环境问题：已向用户报告

**禁止**在未满足前置条件的情况下重触发 CI。

根据步骤 6 的分析结果，按以下流程处理：

### 7.1 判断是否需要修复代码

| 错误类型 | 是否修复代码 | 后续操作 |
|----------|-------------|----------|
| 编译错误 | **是** | 修复代码 → push → 步骤 3 `gp-trigger.sh` |
| UT/ST 执行错误 | **是** | 修复代码或修复用例 → push → 步骤 3 `gp-trigger.sh` |
| 覆盖率不足 | **是** | 补充用例 → push → 步骤 3 `gp-trigger.sh` |
| 代码告警（非误报） | **是** | 修复告警 → push → 步骤 3 `gp-trigger.sh` |
| 代码告警（误报） | 否 | 报告用户后停止 |
| 环境/基础设施错误（已知问题） | 否 | 报告用户后停止 |
| 环境/基础设施错误（偶发） | 否 | 步骤 3 重试（Legacy 用 `gp-api-retry.sh`） |

### 7.2 需要修复代码时：拉取 PR 分支到本地

如果 PR 分支不在本地，先 fetch 到本地：

```bash
# 从模式指导文档步骤 2 的输出中获取分支名（Legacy: ref 字段 / Actions: ref 字段）
BRANCH_NAME="<ref字段值>"

# 注意：如果 PR 来自 fork，origin 可能找不到该分支
# 需要使用 fork remote（如 hgjupstream）或添加 fork remote
git fetch <remote> ${BRANCH_NAME}
git checkout -b ${BRANCH_NAME} <remote>/${BRANCH_NAME}
```

### 7.3 修复代码并 push

1. 根据日志定位具体文件和行号
2. 修复代码
3. 提交并 push：
```bash
git add <修改的文件>
git commit -m "fix: <简要描述修复内容>"
git push <remote> ${BRANCH_NAME}
```

4. push 后重新触发 CI（步骤 3，使用 `gp-trigger.sh`，因为 SHA 已变）

## 实践经验（通用）

> 以下经验来自实际盯 CI 过程中的踩坑总结，执行时务必遵守。模式专属经验见对应指导文档。

### 1. 禁止使用 Python 解析 API 返回值

API 返回的 JSON 字段嵌套复杂且结构可能变化，用 Python 脚本解析极易因字段缺失或 KeyPath 错误而报错或无输出。

**正确做法**：用 `grep -oP` 做简单字段提取。
```bash
# 提取流水线 id 和 status
curl -s "...pipeline?..." | grep -oP '"(id|status)":"?[^",}]+' | head -10

# 从日志中提取错误行
jq -r '.log' /tmp/log.json | grep -iE 'error|fatal|fail' | tail -20
```

### 2. PR 源分支可能来自 fork

如果 PR 的 `head.repo.full_name` 与目标仓库不同（如 `stevenaw0/ge` vs `cann/ge`），说明 PR 来自 fork。此时：
- `git fetch origin` 找不到该分支
- 需要使用已有的 fork remote（如 `hgjupstream`）或添加 fork remote
- push 时要推送到 fork remote 而非 origin

### 3. 本地复现失败用例的约束

禁止直接运行已有的编译产物（stale binary）来复现问题。必须：
1. **先 fetch PR 分支最新代码**到本地（步骤 7.2）
2. **使用项目中的本地测试执行 skill** 编译和运行（不要手动 cmake/make）
3. 只运行失败的 target 或具体用例（`--gtest_filter`），**禁止全量运行**

不遵守此约束的后果：stale binary 可能存在 protobuf 注册冲突、符号未定义、架构不匹配等问题，导致无法正确复现 CI 上的实际失败。

### 4. 禁止未定位根因就重触发 CI

在以下情况下**禁止**重触发 CI：
- 未定位到具体失败的用例名
- 未分析出失败根因（编译错误/断言失败/crash/环境问题）
- 仅凭"可能是偶发"就重试（除非确认是已知偶发环境问题）
- **上一次失败的同名 job 报的是完全相同的错误，且属于配置类**（"项目不在 openlibing 中"、"仓库未注册"、权限/配额）—— 这类错误重试必然复现，直接按「环境/基础设施错误（已知问题）」上报用户后停止

正确做法：先完成分析，再决定是修复代码后 push 还是报告用户。

### 5. 门禁 label 优先于任一系统的 run status

双 CI 并存（过渡期）的仓库里，**单套系统的 run status 可能与门禁结论相反**。判定能否合入只看：

1. `gate`（由 `ci-pipeline-passed` / `ci-pipeline-failed` / `ci-pipeline-running` label 推导）
2. `blocking`（硬阻塞 label）
3. `approval`（lgtm / approve 进度，只存在于 ascend-robot 评论的表格里，label 里没有）

三者一次取齐用 `_gp_pr_meta.sh <PR>`，轮询用 `gp-gate-wait.sh <PR>`。

实测反例（Ascend/torchair PR#3725）：v8 Actions 三次 FAILED 在平台侧未注册的 `sca-pr`/`malicious-scan`
上，Legacy #4123 全绿，门禁 `ci-pipeline-passed`，PR 正常合入。若按 `gpv8-wait.sh` 的
`TERMINAL:FAILED` 结论去改代码，就是修一个不存在的问题。

反过来也要警惕：`gate=passed` **不代表**可以合入。同一个 PR 合入前还带着
`stat/needs-squash`（提示级，平台 merge 时会 squash 消化）；若换成 `needs-issue`
这类硬阻塞，CI 全绿也合不进去。所以 `gp-gate-wait.sh` 在 gate=passed 后仍会检查
blocking 与 approval，不满足时以 **exit 2** 结束（区别于 gate=failed 的 exit 1）。

### 6. 触发 CI 前必须确认没有正在跑的流水线

见步骤 3 的抢占保护。`gp-trigger.sh` 已内置检查（命中则 exit 2），但**人工判断时同样要遵守**：
不要因为在等某一套系统的结果，就去评论 `compile` 重触发 —— 那会取消另一套系统上可能已经全绿的 run。

被抢占的特征：机器人报告里「所有 job ✅ COMPLETED，但顶层 🟨 CANCELED / 已终止运行」。
看到这个组合先怀疑是自己触发的抢占，不要当成代码问题去查。

### 7. 改这些脚本时注意 `set -euo pipefail` 的猝死陷阱

所有脚本都是 `set -euo pipefail`。以下两种写法会在**条件不成立时直接退出脚本**，
表现为"输出到一半就没了、退出码 1"，极易被误判成接口失败：

```bash
# ❌ grep 无匹配返回 1 → pipefail 让管道返回 1 → 赋值返回 1 → set -e 退出
x=$(grep -rE "pattern" "$dir" | head -10)

# ❌ 测试为假时 && 链返回 1 → set -e 退出
[ -n "$v" ] && echo "$v"
```

正确写法：

```bash
x=$(grep -rE "pattern" "$dir" | head -10 || true)     # || true 贴在整个管道末尾
if [ -n "$v" ]; then echo "$v"; fi                    # 用 if，不用 &&
```

历史事故：`gpv8-log.sh` 的三处 `var=$(grep ...)` 就是这么写的，导致**只要日志里没有 gtest 的
`[  FAILED  ]`，error summary 必然为空、退出码 1** —— 编译错误、SCA/配置错误、基础设施错误全中，
而这些恰恰是它的主要用途；后面两级提取和兜底提示全是死代码。已修复。

### 8. 日志与产物不要写 cwd

这些脚本通常必须在仓库里执行（owner/repo 自动探测依赖 `git remote get-url origin`），
所以任何相对路径落盘都会写进用户仓库，`git status` 出现未跟踪目录，一个 `git add -A` 就提交进去。

统一约定：默认落 `${TMPDIR:-/tmp}/gitcode_pipeline_logs/`，用 `GP_LOG_DIR` 覆盖
（`GP_ANALYZE_LOG_DIR` 为兼容别名）。想在任意目录运行则显式设置 `GP_OWNER` / `GP_REPO`。

### 9. smoke/docs 流水线 label 与报告不是真 CI 门禁

Ascend/torchair 等仓库上，PR 创建或 push 后会**自动**触发辅助流水线，产生以下信号：

| 信号 | 实际含义 |
|------|----------|
| label `smoke-pipeline-running` / `smoke-pipeline-success` | 冒烟测试流水线，不参与门禁 |
| label `docs-ci-pipeline-success` | 文档 CI，不参与门禁 |
| AtlasAccount 报告 `PR-pipeline_<repo>_smoke#N ... 运行中` | smoke 流水线报告，不是真 CI |

**历史事故（Ascend/torchair PR#3746）**：`_gp_pr_meta.sh` 输出
`gate=unknown labels=smoke-pipeline-running legacy_report=PR-pipeline_torchair_smoke#619 status=运行中`，
被误判为"真 CI 已自动触发且在跑最新 SHA"，于是跳过步骤 3 直接轮询 —— 实际上真 CI 从未被触发，
`gp-gate-wait.sh` 只会一直等到超时。

**正确判定**：
1. 真 CI 是否在跑/是否通过，**只认 `ci-pipeline-*` label（gate 字段）与真流水线报告**（名字不含 `_smoke`）；
2. `gate=unknown` 且只有 smoke/docs 信号时，结论是**真 CI 尚未触发**，进入步骤 2/3 评论 `compile` 触发；
3. smoke 流水线与真 CI 是不同 workflow，评论 `compile` 不会抢占 smoke，无需等 smoke 结束。

## 环境变量

需要设置 `GITCODE_API_TOKEN`：

```bash
export GITCODE_API_TOKEN="your_token_here"
```

可选：

| 变量 | 作用 | 默认 |
|------|------|------|
| `GP_OWNER` / `GP_REPO` | 跳过 `git remote` 自动探测，从而可在任意目录运行 | 从 origin 推导 |
| `GP_LOG_DIR` | 日志/产物落盘根目录 | `${TMPDIR:-/tmp}/gitcode_pipeline_logs` |
| `GP_ANALYZE_LOG_DIR` | `GP_LOG_DIR` 的兼容别名（`GP_LOG_DIR` 优先） | 同上 |
| `GP_POLL_INTERVAL` | `gp-gate-wait.sh` 轮询间隔（秒） | 60 |
| `GP_BLOCKING_LABELS` | 硬阻塞 label 清单（空格分隔） | `ci-pipeline-failed needs-issue do-not-merge hold` |
| `GP_ADVISORY_LABELS` | 提示 label 清单（空格分隔） | `stat/needs-squash` |
