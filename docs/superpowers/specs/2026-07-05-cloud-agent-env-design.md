# WT9932P4-TINY Cloud Agent 环境（Dockerfile 化）设计文档（Spec）

- **状态**：已实施。原 Dockerfile 化环境（tag-only，2026-07-05）经 Task 7 新会话实测全过（详见 PR #2 正文「验证」表 / 第 8 节）；第十六次追加的 `FROM` digest pin 后，镜像可拉取性与开箱构建**待新 Cloud Agent / 专设 CI 验证**（现有项目 CI 不构建 `.cursor/Dockerfile`）。评审历程见第 13 节、修订记录见第 14 节。
- **关联 Plan**：[`docs/superpowers/plans/2026-07-05-cloud-agent-env.md`](https://github.com/yuangezhizao/WT9932P4-TINY/blob/dev/docs/superpowers/plans/2026-07-05-cloud-agent-env.md)
- **参考实现**：[`yuangezhizao/ESP-Pocket2` PR #1](https://github.com/yuangezhizao/ESP-Pocket2/pull/1)（🐳 Dockerfile 化 Cloud Agent 环境）

---

## 1. 概述

把 WT9932P4-TINY 的 Cloud Agent 开发环境「配置即代码」（Dockerfile 模式），使任何人从对应分支启动 Cloud Agent 都能得到一致、可复现的环境，并让全新环境 `idf.py build` 对 ESP32-P4（含 revision v1.3 工程样片）开箱即用。产物以 draft PR 合入 `dev`。

## 2. 背景与动机

- 现状：仓库无 `.cursor/` 环境定义；Cloud Agent 依赖「个人快照」，需手工安装 ESP-IDF 等，不可复现，他人或新会话无法直接获得一致环境。
- ESP-Pocket2 已用 Dockerfile 模式解决同类问题（PR #1），本项目照此适配。
- 差异动机：本项目芯片是 ESP32-P4（RISC-V）、依赖用 `fetch_repos.py`（非 submodule）、实测样片 revision v1.3，均需针对性适配。

## 3. 需求

功能性：

- R1：仓库自带 Cloud Agent 环境定义，从分支启动即用官方 `espressif/idf:v5.5.4`（`.cursor/Dockerfile` 内 tag+digest 双锁定）镜像构建，无需 dashboard 手工建环境、无需删除个人快照。
- R2：全新环境 `idf.py build` 开箱即用——自动选中 esp32p4，无需手工 `set-target`。
- R3：依赖拉取用 `fetch_repos.py`（幂等、非交互），而非 git submodule。
- R4：支持实测 revision v1.3 芯片（构建 + 本地烧录不被 esptool 拒绝）。
- R5：提供面向人/agent 的 `AGENTS.md` 说明（含 ESP32-P4 QEMU 现状）。

非功能性：

- N1（可复现）：镜像 tag+digest 双锁定（`.cursor/Dockerfile` 的 `FROM` pin 到 `v5.5.4@sha256:b9f2d6ea…`，tag 便于人读、digest 保证字节级不可变）；构建目标由 `sdkconfig.defaults` 固定。
- N2（精简）：只补装基础镜像缺失项，桌面/字体/库由平台 Aptfile 自动提供，不重复安装。
- N3（不破坏现有）：不动 `.devcontainer/`、CI、`.vscode/`、CMake、`fetch_repos.py`、`repos.json`。
- N4（与 CI 一致）：构建目标与 `.github/workflows/build-esp-idf-project.yml` 的 `ESP32P4` 保持一致。

## 4. 设计决策（已与用户确认）

- **D1（sdkconfig.defaults）**：写 `CONFIG_IDF_TARGET="esp32p4"` + `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_0=y`（对应图 1 的 Rev v0.0）。理由：v1.3 属 <3.0 修订族，不声明 `SELECTS_REV_LESS_V3` 则 esptool 以 `requires chip revision in range [v3.0 - v3.99]` 拒绝烧录；`REV_MIN_0`（Rev v0.0）为最低支持修订，兼容 v1.3。
- **D2（git cz 不入镜像）**：`git cz` 是交互式问答，agent/CI 非交互环境无法调用；agent 用 `git commit -m` 手写符合 cz 结构的消息即可。
- **D3（QEMU）**：ESP32-P4 的 `idf.py qemu` 官方尚未支持（work in progress、未文档化），Cloud 内仅做 `idf.py build`；实测基础镜像已自带 `libslirp0` 与 `qemu-system-riscv32/xtensa`，故不重复安装、也不为 P4 做 QEMU 验收；`esp-emulator` 仅作可选说明。
- **D4（排查工具）**：以 ESP-Pocket2 CLI 集为参考，按 `espressif/idf:v5.5.4` 容器实测仅补装缺失项；`less/ccache/git-lfs/libslirp0/QEMU` 实测已自带、不重复安装。
- **D5（PR 形式）**：分支 `cursor/cloud-agent-env-5858`，draft PR → `dev`，标签 `enhancement`，指派 `yuangezhizao`，标题 `docker(cloud-env): 🐳 …`。

## 5. 架构与组件

四个文件，各自单一职责、通过明确边界协作：

- **`.cursor/environment.json`**：Cloud Agent 运行配置（选镜像 + `install` 拉依赖）。被 Cursor 平台读取；依赖 `.cursor/Dockerfile` 与 `fetch_repos.py`。
- **`.cursor/Dockerfile`**：镜像定义（`FROM espressif/idf:v5.5.4@sha256:b9f2d6ea…` tag+digest 双锁定 + 补装 CLI + `/etc/bash.bashrc` 自动 source `export.sh`）。被 `environment.json` 的 `build` 引用；依赖官方镜像与平台 Aptfile 自动装的桌面栈。
- **`sdkconfig.defaults`**：构建目标与修订声明。被 `idf.py` 首次构建/reconfigure 读取；依赖 ESP-IDF Kconfig。
- **`AGENTS.md`**：面向人/agent 的说明。被 Cloud Agent 读取；内容依赖上述三者的事实（路径、依赖机制、QEMU 现状）。

## 6. 控制流（Cloud Agent 启动生命周期）

1. 从分支起 Cloud Agent → Cursor 读仓库 `.cursor/environment.json`（解析优先级最高，覆盖个人快照，后者仅 fallback）。
2. build：从 `.cursor/Dockerfile` 构建镜像（layer 缓存，改动只重建变化层）。
3. checkout：Cursor 把正确 commit 检出到工作区（不由 Dockerfile `COPY`）。
4. install：在仓库根运行 `python3 fetch_repos.py --yes`（同步阻塞、需幂等；`repos.json` 为空则跳过；耗时>数秒会被快照缓存）。
5. 就绪：新 shell 经 `/etc/bash.bashrc` 自动 `source export.sh`，`idf.py` 可用。
6. 开发：`idf.py build` 读 `sdkconfig.defaults` → esp32p4，产物 `build/WT9932P4-TINY.bin`。

## 7. 错误处理与边界情况

- **P4 无 `idf.py qemu`**：`AGENTS.md` 明确 Cloud 内不做 QEMU 冒烟，仅 `idf.py build`；无硬件仿真可选 `esp-emulator`（非默认组成）。
- **revision 校验**：`SELECTS_REV_LESS_V3=y` 保证 v1.3 可烧录；纯构建不受该项影响。**rev <3.0 与 ≥3.0 互斥**——本配置面向 <3.0 样片，换 ≥3.0 量产芯片需切换 revision 配置。
- **快照回退**：若缓存快照失效，Cursor 回退到基础镜像并重跑 `install`；`fetch_repos.py` 幂等，可在启动时自愈。
- **误提交生成物**：`sdkconfig`、`dependencies/`、`managed_components/` 均已被 `.gitignore` 忽略；提交时避免 `git add -A` 误加。
- **PR 工具仓库名大小写（工具链 quirk）**：git remote 为小写 `wt9932p4-tiny`，而 PR 链接用规范大写 `WT9932P4-TINY`；`ManagePullRequest` / `EditPullRequestLabels` 对 `pr_url` 做大小写敏感比较，传大写会报 `PR URL must belong to the current repository`。绕过：`pr_url` 的仓库 slug 用与 remote 一致的小写重试即成功（GitHub 与 `gh` 本身不敏感）。本会话与 Task 7 新会话均复现，不影响环境/构建。

## 8. 测试与验证策略

- **CI**（PR → `dev`）：matrix `v5.5.4`（必过）+ `latest`（experimental，允许失败）；跑 `idf.py build` + `size` + `esptool image-info`。注意 CI 的 `esp-idf-ci-action` 显式传 `target: ESP32P4`，故 CI 只证明「项目在 esp32p4/v5.5.4 下可构建」，**不**证明「开箱即用（无需 set-target）」与 Dockerfile 生效——后两者由新会话验证。
- **环境验证（需新会话）**：从本分支起 Cloud Agent，核对 `git -C /opt/esp/idf describe --tags`=v5.5.4、`idf.py build` 开箱、产物生成、`sdkconfig` 含 revision 配置、`esptool image-info`（4.x/5.x 兼容）的 chip rev 范围落在 <3.0；回填 PR。非交互 shell（`bash -lc 'command -v idf.py'`）为**诊断项（非阻断）**，硬验收以 `idf.py build` 成功为准。（详见 plan Task 7 提示词与验收标准）
- **（spec 编写阶段说明，历史）该会话不做真实 build 验证**：编写本 spec 时的会话为旧个人快照（IDF 5.4，非目标镜像），其结果不代表 Dockerfile；真实验证见下方「实测结果」（Task 7 新会话，2026-07-05）。

### 实测结果（2026-07-05，本分支 Cloud Agent）

> **本表为 digest pin 之前（tag-only，2026-07-05）的实测快照**，完整证据留档于此、避免脱离 PR #2 后无法追溯。环境：从 `cursor/cloud-agent-env-5858` 分支启动的**全新** Cloud Agent，环境由 `.cursor/Dockerfile`（`FROM espressif/idf:v5.5.4`）构建，全程**未手动 `set-target`**。（同一实测的表格亦回填于 PR #2 正文「验证」章节，PR 版含 `✅` 标注；digest pin 后的验证见本节表后注记。）

| # | 命令 / 检查 | 结果（证据） |
|---|---|---|
| 1 | 环境来源确认（无回退基础镜像） | Dockerfile 特有包 `sudo`/`dfu-util`/`jq`/`htop`/`file`/`lsof` 全部存在；`/etc/bash.bashrc` 末尾含 `. /opt/esp/idf/export.sh`；`install` 日志「repos.json 为空跳过」、status=0 → 确由 Dockerfile 构建、无回退 |
| 2 | `git -C /opt/esp/idf describe --tags` | `v5.5.4` |
| 3 | `idf.py --version` | `ESP-IDF v5.5.4` |
| 4 | `python3 fetch_repos.py --yes` | `repos.json 中未配置任何依赖, 无需处理。`，exit=0（幂等、跳过） |
| 5 | `idf.py build`（未 set-target） | 日志 `-- IDF_TARGET is not set, guessed 'esp32p4' from sdkconfig '/workspace/sdkconfig.defaults'` → `Project build complete`；生成 `build/WT9932P4-TINY.bin`（app `0x31a60` bytes，app 分区 81% free） |
| 6 | `idf.py size` | Total image size `203008` bytes（Flash `131986` / DIRAM `77144`）；bootloader `0x5980`、app `0x31a60`；exit=0 |
| 7 | 精确校验 3 条 `sdkconfig`（连值一起，命中注释不算） | `CONFIG_IDF_TARGET="esp32p4"`、`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`、`CONFIG_ESP32P4_REV_MIN_0=y` → `sdkconfig OK`；派生 `CONFIG_ESP_REV_MIN_FULL=0`、`CONFIG_ESP_REV_MAX_FULL=199`（支持 v0.0–v1.99） |
| 8 | `esptool image-info`（bootloader + app 两产物） | esptool `v4.12.dev1`（走 `image-info` 连字符子命令）；两产物 `Chip ID: 18 (ESP32-P4)`、chip revision `v0.0 – v1.99`（落在低于 3.0、非 `v3.0 – v3.99`）；checksum / validation hash 均 valid；app 头部 `Project name: WT9932P4-TINY`、`ESP-IDF: v5.5.4` |
| 9 | `bash -lc 'command -v idf.py && idf.py --version'`（诊断，非阻断） | 找到 `/opt/esp/idf/tools/idf.py`、`ESP-IDF v5.5.4` → 非交互 login shell 亦可用（IDF 环境靠平台注入环境变量继承，不依赖 `/etc/bash.bashrc`） |

**结论**：全部必过项通过、诊断项亦通过。Dockerfile 化环境**开箱即用**——全新环境无需 `set-target` 即可 `idf.py build` 成功产出 `WT9932P4-TINY.bin`，bootloader/app 芯片修订范围正确落在低于 3.0（适配 v1.3 样片）；诊断项 9 顺带确认 `bash -lc` 也能用 `idf.py`，故无需在 Dockerfile 补 `/etc/profile.d/*.sh` 或 `ENV PATH`。

> 注：上表为 **digest pin 之前**（2026-07-05）的实测快照；第十六次给 `.cursor/Dockerfile` 的 `FROM` 补 `@sha256:b9f2d6ea…` 后，digest 可拉取性与开箱构建由新开 Cloud Agent 或专设 CI 另行验证（现有项目 CI 不构建 / 拉取 `.cursor/Dockerfile`）。

## 9. 范围与非目标（YAGNI）

- 范围内：新增上述 4 个文件。
- 非目标（本次不做）：改 `.devcontainer/`（ESP-IDF 自动生成）、CI、`.vscode/`、`fetch_repos.py`、`repos.json`、CMake；为 P4 启用/验收 QEMU（镜像虽自带 `qemu-riscv32/xtensa` 与 `libslirp0`，但 idf.py qemu 对 P4 未支持）、额外安装 `esp-emulator`/`git cz`/Tailscale/cloudflared（未来需要时再在 Dockerfile 增补）。

## 10. 会话 QA（设计相关问答）

- **Q1（当前会话开发 vs 新开会话）**：编写并提交本 PR 当前会话即可（只是写文件 + git）；但验证「开箱即用」必须新开一个「从本 PR 分支启动」的 Cloud Agent——Dockerfile 模式只在新会话从带 `.cursor/` 配置的分支启动时才构建，旧个人快照会话不会重建。三个工具角色不同：`idf` 由镜像内置（无需重装）、`clangd` 是本地 Cursor 客户端的 IntelliSense（跑在本地、与 Cloud 无关）、`git cz` 交互式 agent 用不了。背景：本会话已实现并合并 PR #1、现做独立的 PR #2，上下文占用约 31% 且关键决策已固化在 spec/plan，故当前会话继续编写合适；唯一必须新会话的是 Task 7 的 Dockerfile 验证。
- **Q2（当前 Cloud 环境预置工具、与之前差异）**：以容器实测为准（见第 14 节第九次）——`espressif/idf:v5.5.4` 镜像自带 `git/less/ccache/git-lfs/libslirp0/qemu-riscv32+xtensa`，缺 `sudo` 及 `htop/vim/file/lsof/jq/net-tools/iproute2/dnsutils/dfu-util`。平台 `vnc-desktop.Aptfile` 相比 ESP-Pocket2 记录新增「Cloud asset installer prerequisites」节（`bash/ca-certificates/coreutils/curl/findutils/gzip/tar`）。
- **Q3（PR 标题「新版 cz 格式」）**：`cz-conventional-emoji@1.1.8` 的 `engine.js` 生成 `typeName(scope): emoji subject`；`🐳 :whale:` 不在 `GITMOJI_TYPE_MAP`，选它时 `typeName` 回退为 `docker`。故采用 `docker(cloud-env): 🐳 …`（备选 `chore(cloud-env): 🔧 …`）。
- **Q（superpowers 流程）**：标准流程为 brainstorming → spec → writing-plans → plan。本 spec 系补齐：先前对话直接产出了 plan（会话开始时只有单一 plan 文件），后按规范把 QA/设计决策/评审历程/修订记录等迁移至本 spec；两者内容一致（本 spec 讲「是什么/为什么」，plan 讲「怎么做」的逐步实现）。
- **Q（skills 可用性）**：环境含 superpowers 系列 14 个 skill（brainstorming / writing-plans / executing-plans / subagent-driven-development / using-git-worktrees / finishing-a-development-branch / requesting-code-review / receiving-code-review / test-driven-development / systematic-debugging / verification-before-completion / dispatching-parallel-agents / using-superpowers / writing-skills），本任务所需的 brainstorming/writing-plans/executing-plans/subagent-driven-development 均在。
- **Q（执行方式）**：二选一。**① `executing-plans`**（当前会话批量执行 + 人工 checkpoint）：开销低、上下文连贯、人可控，适合任务少或需人频繁把关。**② `subagent-driven-development`**（每任务派 fresh 子代理 + 两阶段审查：spec 合规 → 代码质量）：质量门禁强、主上下文干净，但子代理调用多、开销大。本任务量小（4 源码文件；3 实现 + 1 文档 = 共 4 提交）、无代码逻辑/无单测、真正的「开箱即用」验证只能靠新开 Cloud Agent，故 ② 性价比低——**倾向 ①**。

## 11. 调研依据（摘要）

- Cursor 官方文档（Cloud Environment Setup）：`build.dockerfile/context` 相对 `.cursor/`；`install`(=update) 同步阻塞、需幂等、>数秒被快照；Dockerfile 须含 git+sudo；不要 COPY 源码；Computer Use 需 Debian/Ubuntu。
- ESP-IDF 官方（ESP32-P4 host-apps / QEMU）：P4 的 `idf.py qemu` 仍在开发、未文档化；`espressif/esp-emulator` 支持 P4。
- ESP32-P4 revision Kconfig：`SELECTS_REV_LESS_V3` 支持 0.x/1.x（与 >=3.0 互斥）；`REV_MIN_0`=Rev v0.0（依赖 SELECTS_REV_LESS_V3），本项目显式写入。
- cz-conventional-emoji@1.1.8（`engine.js`）：格式 `typeName(scope): emoji subject`；`🐳 :whale:` 不在映射表 → 回退名 `docker`。
- 平台 Aptfile（`/usr/local/share/vnc-desktop.Aptfile`）：相比 ESP-Pocket2 记录新增「Cloud asset installer prerequisites」节。

## 12. Task 工具与 model slug（参考）

> 本节记录本任务中用到的 Task 子代理工具知识（回答用户提问所得），供后续参考。

### Task 工具简介

**本质**：Task 工具的唯一功能是**启动（或用 `resume` 恢复）一个子代理（subagent）**，把复杂/多步任务委派给它自主完成。它本身不直接读写文件、不跑命令（那些由主 agent 用 `Shell`/`Read`/`StrReplace` 做，或由子代理在其自身执行环境里做）；能力多样性来自不同的 `subagent_type`。

| `subagent_type` | 用途 | 只读 |
| --- | --- | --- |
| `explore` | 快速探索代码库（找文件、搜关键词、答代码问题） | 是 |
| `generalPurpose` | 通用：复杂研究、多步搜索与任务（本计划各轮文档 review 用它） | 否（可配 `readonly`） |
| `code-reviewer` | 阶段完成后对照 plan/规范审查代码 | 否 |
| `cursor-guide` | 读 Cursor 产品文档答疑 | 是 |
| `bugbot` | Bugbot 式本地改动审查（需显式要求） | 通常只读 |
| `security-review` | 安全审查（需显式要求） | 通常只读 |
| `best-of-n-runner` | 在隔离 git worktree 跑任务（best-of-N 并行/实验） | 否 |

关键参数：`subagent_type`（必填）、`prompt`（自包含任务描述——子代理**不继承**当前会话上下文）、`description`、`model`（可选，见下表；不填=同当前会话）、`readonly`（只读/Ask 模式，禁写、无 MCP/联网）、`resume`（用 agent ID 续跑并保留其上下文）、`file_attachments`。

### 可传入的 model slug 列表（2026-07-05）

| 家族 | 可用 slug |
| --- | --- |
| Claude | `claude-4.6-sonnet-high-thinking`、`claude-opus-4-7-thinking-high`、`claude-opus-4-7-thinking-high-fast`、`claude-opus-4-8-thinking-high`、`claude-opus-4-8-thinking-high-fast` |
| Composer | `composer-2.5`、`composer-2.5-fast` |
| GPT | `gpt-5.3-codex-high`、`gpt-5.3-codex-high-fast`、`gpt-5.4-high`、`gpt-5.4-high-fast`、`gpt-5.5-high`、`gpt-5.5-high-fast` |

> 客户端的「1M context / Extra High / MAX Mode」是运行时设置，无法作为 Task 参数；Task 侧同一模型只到 `-high`（及 `-fast`）粒度。不指定 `model` 时子代理沿用发起该 Task 的当前会话模型。

## 13. GPT 交叉评审历程（供后续参考）

本计划编写后，采用**独立子代理交叉评审**（而非当前会话自审）反复打磨。

### 方法（Cross-Context Review）

- 每轮用全新 `gpt-5.5-high` / `gpt-5.5-high-fast` 子代理（Task 工具，`readonly`）只读评审 plan + spec；子代理**不继承**主会话（Claude）上下文，只拿到自包含背景 prompt。
- 换模型（GPT 评审 Claude 产出）+ 换上下文，规避「自审锚定偏差」。
- 依据（联网检索）：Cross-Context Review 对照实验显示独立会话评审 F1 显著高于同会话自审、甚至高于「带上下文子代理」；同会话重复自审无改善。Mirror Agent 实测 fresh-context 多抓约 2.3× 错误。配合 adversarial framing、precision-over-recall、human-on-merge。

### 各轮结果

| 轮 | 模型 | 发现 | 严重 | 主题 |
| --- | --- | --- | --- | --- |
| 1 | gpt-5.5-high | 11 | 0 | 任务顺序、校验命令必失败、CI 过度推断、Task 7 验收不足 |
| 2 | gpt-5.5-high-fast | 8 | 0 | git fetch 基点、awk 锚定、grep 退出码、esptool fallback、bash-lc 降诊断 |
| 3 | gpt-5.5-high-fast | 5 | 0 | Dockerfile 校验退出码、esptool if/else、spec 同步、去硬编码模型名 |
| 4 | gpt-5.5-high-fast | 4 | 0 | esptool 补 app bin、sdkconfig grep-qx、label/repo 拆分 |
| 5 | gpt-5.5-high-fast | 5 | 0 | for 循环退出码、Task7 校验与 Task1 一致、目视项、Step2 && |
| 6 | gpt-5.5-high-fast | 1 | 0 | libslirp0/QEMU 口径措辞（不臆断镜像）|
| 7 | gpt-5.5-high-fast | 0 | 0 | 可进入执行阶段 |
| 8 | gpt-5.5-high-fast | 3 | 0 | 容器实测更新引入的一致性遗漏（Task4/Task2 校验、D4/Q2 表述），已修 |
| 9 | gpt-5.5-high-fast | 1 | 0 | 确认第八轮修订到位、可进入执行；1 次要（sudo 注释措辞）已修 |
| 10 | gpt-5.5-high-fast | 0 | 0 | 无实质问题，可进入执行阶段 |
| 11 | gpt-5.5-high-fast | 2 | 0 | 文档结构重构（QA/记录迁移至 spec）后，2 条交叉引用残留（plan header「见第六节」、第四节「对话对比」），已修 |
| 12 | gpt-5.5-high-fast | 0 | 0 | 重构后复核：跨文件引用/内容一致性/Task 0–7 可执行性均无实质问题，**可进入执行阶段** |
| 13 | gpt-5.5-high-fast | 0 | 0 | 复核 Claude 主会话自审修的 5 项（Task0 `git status` / PR 模板 Step0 / assignee 手动说明 / 执行方式优缺点入 spec §10 / Self-Review 口径）均到位，**可进入执行阶段** |
| 14 | gpt-5.5-high-fast | 0 | 0 | 复核 Claude 第二轮自审修的 4 项（label 依赖 `pr_url` / 提交=冻结评审 / Task7 用 `update_pr` 回填 / commit 措辞「新增」）均到位，**可进入执行阶段** |

> 期间做了一次**容器实测**（`podman run --rm --network=none espressif/idf:v5.5.4`），颠覆了对镜像内容的假设（libslirp0/qemu/less/ccache/git-lfs 实测已自带），据实精简 Dockerfile 并修口径——见第 14 节第九次。说明：**关键事实要用真环境实测求证**，光靠多轮文本评审不够。

### 关键收获与复用建议

- 交叉评审持续抓到主会话自审的盲点，且**多为自己修订时引入的新 bug**（退出码、awk 匹配范围、fallback 逻辑、一致性遗漏）——印证「fresh eyes」价值。
- 问题从「设计/表述」逐步收敛到「shell 退出码/措辞口径/一致性」细节；每次较大改动后都可能带出新一致性问题，故改动后值得再跑一轮独立评审。
- 复用做法：改动 plan/代码后 → 派独立子代理（尽量换模型）readonly 评审 → 对反馈做技术核验（**不盲从**，如本例对「镜像自带 libslirp0」用容器实测求证）→ 只采纳真问题 → 收敛后记录。

## 14. 修订记录

> 本计划与 spec 在编写后经历多轮用户确认与 GPT 交叉评审的迭代，逐次修订如下（原记录于 plan「本轮更新」，现归档至此）。

### 第二次（初次 review 后）
- [Q1 最终] `sdkconfig.defaults` 写入 `SELECTS_REV_LESS_V3=y` 与 `REV_MIN_0=y`（用户复核后加回 REV_MIN_0，与图 1 的 Rev v0.0 一致）。
- [Q2 流程] 补齐 spec（superpowers 标准流程 brainstorming→spec→writing-plans→plan）。
- [Q3 skills] 确认环境含 superpowers 14 个 skill。
- [Q4 执行方式] `executing-plans` vs `subagent-driven-development` 对比，本任务倾向 executing-plans。

### 第三次（GPT 首轮评审）
- 新增 Task 0 先建分支、Task 5 不写死 commit hash（#1、#11）；修 libslirp0/submodule 校验（#2、#3）；修正「CI 证明开箱即用」（#6）；Task 7 加 revision + 非交互 shell 验证（#5、#8）；rev 互斥提醒（#7）；统一 4 提交（#9）；新增 slug 列表。

### 第四次（GPT 二轮）
- `git fetch origin`；awk 锚定 `^RUN`；sdkconfig 校验 `^…=`；AGENTS 校验 `test -eq 0`；esptool 4.x/5.x fallback；`bash -lc` 降诊断项；PR 文案「git 依赖镜像自带」；去硬编码模型名。

### 第五次（GPT 三轮）
- Dockerfile 校验命中 libslirp0 时 `exit 1`；Task 7 Step 3 esptool 改 `if/else`；spec 同步 `bash -lc` 诊断项；彻底删硬编码模型名；spec 状态改「已定稿·待执行」。

### 第六次（GPT 四轮）
- Task 7 esptool 扩展到 bootloader + app 两个产物；sdkconfig 校验 `grep -qx`；PR Step 2 拆分 label/repo；澄清「删除的是默认模型名，slug 列表保留为参考」。

### 第七次（GPT 五轮）
- Task 7 esptool `for` 循环加 `|| exit 1`；Task 7 sdkconfig 校验改 `grep -qx` 与 Task 1 一致；明确 esptool image-info 为目视项；Step 2 改 `&&`；spec 状态去写死轮数。

### 第八次（GPT 六轮）
- 统一 libslirp0/QEMU 口径为「本 PR 不额外安装/不依赖/不验收；基础镜像是否自带不影响定位」（当时尚未实测，故不臆断镜像）。第七轮评审判「可进入执行」。

### 第九次（容器实测 espressif/idf:v5.5.4）
- 用 `podman run --rm --network=none` 实测：镜像**已自带** `libslirp0`（4.7.0）与 `qemu-system-riscv32/xtensa`（`/opt/esp/tools/qemu-*`）、`git/less/ccache/git-lfs`；**缺** `sudo` 及 `htop/vim/file/lsof/jq/net-tools/iproute2/dnsutils/dfu-util`。据此把 Dockerfile apt 列表精简（移除冗余的 `less/ccache/git-lfs`）、口径改为「镜像已自带/已内置」。IDF=`v5.5.4`，OS=Ubuntu 24.04.4 LTS。

### 第十次（第八轮评审 + 归档）
- 第八轮评审修 3 条「实测更新引入的一致性遗漏」（Task 4 校验去 `qemu-system-xtensa`、Task 2 校验加 `less/ccache/git-lfs`、D4/Q2 表述同步）。第九、十轮均判「可进入执行阶段」。

### 第十一次（文档结构重构）
- 按 ESP-Pocket2 的 superpowers 规范，把原写在 plan 里的 QA（Q1/Q2/Q3）、设计决策、修订记录、GPT 交叉评审历程、Task 工具/slug 参考等**迁移至本 spec**；plan 精简为只含「文件结构 + 任务分解 + 执行方式 + Self-Review」。

### 第十二次（自审 + 文档一致性修复）
- Claude 主会话自审发现并修 5 项：Task 0 加 `git status --porcelain` 显式检查工作区；Task 6 加「Step 0 检查 PR 模板」、并明确 assignee 需用户在 GitHub UI 手动设置（当前 `ManagePullRequest`/`EditPullRequestLabels` 不支持 assignee、`gh` 只读）；把执行方式优缺点完整写入本 spec §10 Q「执行方式」、plan 第四节改为引用该摘要；Self-Review 的「去 libslirp0」口径改为「不重复装 libslirp0、不做 P4 QEMU 验收」。

### 第十三次（自审第二轮修复）
- Claude 主会话第二轮自审修 4 项：Task 6 加 label 明确依赖 Step 1 返回的 `pr_url`（`EditPullRequestLabels` 需 `pr_url`；已 `gh` 核验仓库存在 `enhancement` label）；Task 5 Step 6 补「提交=冻结评审」说明（提交后除 Task 7 回填外不再改 spec/plan）；Task 7 提示词/Step 4 明确用 `ManagePullRequest` 的 `update_pr` 回填 PR 正文；Step 6 的 commit message「补充」改「新增」。

### 第十四次（执行阶段：为 4 个提交补充 commit body）
- 背景：plan 执行完成、PR #2 建立后，用户发现 4 个提交仅有标题、无 body（对比 ESP-Pocket2 旧 PR 的 `-` 列表风格）。经联网查证：Conventional Commits 规范中 body 为可选（MAY），但主流最佳实践（cbeams 七条、GitHub 博客等）建议用 body 解释 what/why（尤其非显然的设计取舍），且与用户既有风格及本仓库规则（`-` 并列多点）一致，故补充 body。
- 手段：因平台改写了 `GIT_SEQUENCE_EDITOR`（`git rebase -i` 不可控），改用 `git reset --soft origin/dev` 解散后按 Task 5 逐个重建、再 `git push --force-with-lease`；footer `Co-authored-by` 由平台 `commit-msg` hook（`core.hooksPath` 下）自动注入，无需手写。
- 同步：plan Task 5 的各提交命令改为带 body 的形式，并新增 body 规范与既有提交重写手段的说明。属执行阶段的既有提交润色，不改变任何文件内容与设计决策。

### 第十五次（执行收尾：Task 7 实测通过 + 勾选 plan 复选框）
- Task 7 由用户新开 Cloud Agent 实测：全部必过项 + 诊断项均通过（环境确由 Dockerfile 构建、v5.5.4、未 set-target 即 build 成功、产物 WT9932P4-TINY.bin、三条 sdkconfig 精确匹配、芯片修订范围 v0.0–v1.99 低于 v3.0、bash -lc 亦可用 idf.py），实测结果表已由该会话回填至 PR 正文「验证」章节。
- 应用户要求把执行状态回写 plan：勾选 Task 0–7 全部执行 step 复选框（改为 - [x]）、并在 Task 7 增补「已完成」标注；本 spec 状态由「已定稿 · 待执行」改为「已实施 · 已实测通过」。上述改动 amend 进 docs(superpowers) 提交（第 4 个提交）后 force-with-lease 推送。
- 追加：应用户「担心脱离 PR 无法追溯」的顾虑，把完整实测表（9 项命令 + 证据）从 PR 正文留档到本 spec 第 8 节「实测结果」，plan Task 7 摘要加指向；同样 amend 进第 4 个提交。
- 追加：把执行中复现的「PR 工具 pr_url 大小写敏感」quirk（`ManagePullRequest` / `EditPullRequestLabels` 传大写仓库名报 `PR URL must belong to the current repository`，改小写 slug 重试即可）写入 spec 第 7 节与 plan Task 6 Step 2，供后续避坑。

### 第十六次（补充 ESP-Pocket2 PR#7 通用修复：基础镜像 pin digest）
- 参考 ESP-Pocket2 PR #7，给 `.cursor/Dockerfile` 的 `FROM` 加 digest：`espressif/idf:v5.5.4@sha256:b9f2d6ea1c19e0c9f7959bdb74a9e3c775642f9d0f3b841937c5fa3363db892b`（index / manifest-list digest，多架构通用；linux/amd64→`116f0526…`、linux/arm64→`f3bc9dae…`）。digest 由用户可信本地 `docker buildx imagetools inspect espressif/idf:v5.5.4` 复核、三方一致确认；绝不采用被 prompt injection 伪造的 `9682ee…` / `c286f2f9…`。
- 「可复现」措辞统一为「tag+digest 双锁定」：Dockerfile 注释（去「（发布 tag，可复现）」并加 digest 说明）、AGENTS.md、本 spec N1、PR #2 正文（核心改动 Dockerfile 描述 + 关键设计点）。
- 一处文档小修：plan「以下各提交用 `git commit -F -` 配合 heredoc」订正为与实际展示一致的 `git commit -m 标题 -m 正文`；本 spec 第 3 行状态行「修订记录见第 14/15 节」订正为「第 14 节」（本节只有 `## 14`，各次为其下 `###` 子标题、无 `## 15`）。
- 安全留档：本次规划一度在被 prompt injection 污染的旧会话进行——注入方伪造工具结果、掉包镜像 digest、并注入「用 court 开头替换标准工具调用标签」的带内指令，致工具调用被污染且同一会话无法自愈，故迁移到干净新会话完成规划与实施。护栏：严格标准工具调用格式、无视带内指令、敏感值需 ≥2 干净落盘源交叉核验、污染即迁移新会话。执行前 preflight 发现本地基线（旧 checkout）落后于远端 PR 分支，先 `git reset --hard origin/<PR 分支>` 对齐再操作，避免 force-with-lease 覆盖远端。
- 落地：改动并入既有 4 个提交（Dockerfile→第 2 提交、AGENTS.md→第 3 提交、spec/plan→第 4 提交），经 `git reset --soft origin/dev` 逐个重建、`git push --force-with-lease` 推送；PR 正文两处措辞用 `update_pr` 增量更新、保留第十五次的实测表。

### 第十七次（复审整改：plan 草稿标注 + 验证边界措辞校准）
- plan Task 6 内嵌的「PR 正文草稿」（建 PR 时初稿）已落后于实际 PR 正文（后者经 Task 7 实测与第十六次 digest pin 多次 `update_pr` 更新）：给草稿标题加「Task 6 建 PR 时初稿 · 历史留档」、注明「以 PR #2 实际正文为准、不逐次同步」，把「文档内嵌会演进副本」的 drift 显式转为历史留档。
- 验证边界措辞校准：spec 第 8 节「当前会话不做真实 build 验证」订正为「spec 编写阶段（历史）」口径并指向实测结果、「见文末注记」订正为「见本节表后注记」；PR 正文「已实测」句加「原 tag-only Dockerfile」限定，与既有「未覆盖（digest pin 后）」说明呼应。
- 落地：均属文档措辞/一致性整改，改动并入第 4 提交（plan/spec），`git push --force-with-lease` 推送；不改任何源码/配置文件与设计决策。

### 第十八次（精简 commit message）
- 应用户「commit 信息冗余」反馈，从第 4 提交（docs(superpowers)）message body 移除「含第十五/十六/十七次修订」三行摘要；逐次修订的详细记录仍完整保留于本节（§14），commit body 只保留提交本身的说明（新增 spec/plan、参照规范、置于末尾）。
- 同步 plan Task 5 Step 6 展示的 commit body 一并移除该三行，保持「plan 展示 == 真实 commit」；改动并入第 4 提交、`git push --force-with-lease`；不改源码/配置与设计决策。
