# WT9932P4-TINY 新增板载 RGB blink 例程 设计文档（Spec）

- **状态**：已实施完成（2026-07-12）。分支 `cursor/add-blink-example-6f89`，合并为 3 个提交（C1 refactor(main_hello_world.c) 重命名 / C2 feat(main_blink_example.c) blink 例程 / C3 docs(docs/superpowers) 文档），PR #3 → `dev`（创建时为 draft，现已标记 ready for review）；本 spec/plan 已随 C3 入库。实施详情见 plan 文末「执行结果」。
- **关联 Plan**：[`docs/superpowers/plans/2026-07-07-blink-example.md`](https://github.com/yuangezhizao/WT9932P4-TINY/blob/dev/docs/superpowers/plans/2026-07-07-blink-example.md)
- **参考实现**：main 分支 [`blink/`](https://github.com/yuangezhizao/WT9932P4-TINY/tree/main/blink)（即 ESP-IDF 官方 Blink 示例，已配置为板载 RGB：`led_strip` + GPIO51）

---

## 1. 概述

把 main 分支的 blink 例程移植进 dev 分支，并**融入 dev 既有的"单项目 + `SRCS` 注释切换 demo"脚手架模式**（而非照搬 main 的独立工程目录）。blink 点亮板载 RGB（可寻址 `led_strip`，GPIO51，RMT backend，周期 1000ms），依赖 `led_strip` 以 managed component 引入并**升级到 `^3.0.3`**。移植同时确立 `main_<demo>.c` 源文件命名约定（顺带把现有 `hello_world_main.c` 重命名为 `main_hello_world.c`），并让 blink 成为**默认激活**的 demo。

## 2. 背景与动机

- dev（当前分支）是"单项目脚手架"：仓库根一个 `project(WT9932P4-TINY)`，实际生效的 `app_main` 由 `main/CMakeLists.txt` 的 `SRCS` 列表中未被注释的那个源文件决定，其余以注释形式并列为备选（见 `AGENTS.md`）。现有源文件为 `hello_world_main.c`（生效）与 `main.c`（空 `app_main`，注释备选）。
- main 分支是"多项目仓库"：顶层并列 `blink / lvgl_demo_v8 / lvgl_demo_v9 / video_lcd_display` 四个独立 ESP-IDF 工程 + `common_components/`。其中 blink 是标准官方 Blink 示例，自带完整 `CMakeLists.txt`、`sdkconfig.defaults(.esp32p4)`、`main/Kconfig.projbuild`、`main/idf_component.yml`。
- 目标：在 dev 上以最小侵入的方式新增 blink 作为一个可切换 demo 并默认启用，让"从本分支起 `idf.py build` / CI"开箱即构建 blink，用于验证板载 RGB。

## 3. 需求

功能性：

- R1：dev 的 `main/` 下新增 blink demo 源文件 `main_blink_example.c`（内容源自 main 分支 `blink/main/blink_example_main.c`，逐字不改），点亮板载 RGB。
- R2：blink 融入 `SRCS` 切换模式，并作为**默认激活**的 demo。
- R3：`led_strip` 依赖以 managed component 引入（新建 `main/idf_component.yml`），版本 `^3.0.3`；**不提交** `dependencies.lock`（不 `git add`、不锁定解析版本；本 PR 不将其加入 `.gitignore`，见 D12）。
- R4：blink 的 Kconfig 原样移植为 `main/Kconfig.projbuild`，`menuconfig` 可调 LED 类型 / backend / GPIO / 周期。
- R5：blink 默认配置（`CONFIG_BLINK_LED_STRIP=y`、`CONFIG_BLINK_GPIO=51`）写入根 `sdkconfig.defaults`（作为默认激活 demo 的默认值）；同时把既有的 P4 revision 两项移入**新建**的 `sdkconfig.defaults.esp32p4`（见 D8）。
- R6：确立 `main_<demo>.c` 命名约定，把现有 `hello_world_main.c` 重命名为 `main_hello_world.c`（内容不变）。
- R7：更新 `AGENTS.md` 的"选择运行哪个 demo"章节，纳入 blink 说明与命名约定。
- R8：提交本次 superpowers 流程新增的文档（本 spec 与配套 plan）入库。提交时机依 D10/D11：spec/plan 阶段暂不提交（供工作区 Review），最终随实施阶段 PR 的收尾提交一并纳入 git（与仓库先例一致——上次 cloud-agent-env 的 spec/plan 已入库）。

非功能性：

- N1（不破坏现有）：`main_hello_world.c` 重命名后仍可编译；`main.c` 保持原名与内容；根 `sdkconfig.defaults` 的 `CONFIG_IDF_TARGET` 保留（P4 revision 两项按 D8 迁入 `sdkconfig.defaults.esp32p4`，单一 P4 target 下构建行为等价、不影响烧录校验）。
- N2（开箱即用）：新环境 / CI 执行 `idf.py build` 开箱即得"blink + LED strip + GPIO51"，无需手工 `menuconfig`。依赖版本**不锁定**（不提交 `dependencies.lock`），构建时按 `^3.0.3` 语义解析最新兼容 3.x。
- N3（与 CI 一致）：默认 `SRCS` = blink，使 CI（`.github/workflows/build-esp-idf-project.yml`）构建目标即 blink，把 blink 的可编译性纳入 CI 覆盖。
- N4（最小侵入）：只新增/改动必要文件；不引入 `common_components/bsp_extra`（blink 不依赖它）；不改 CI / `.devcontainer` / `.vscode` / `fetch_repos.py` / `repos.json` / 顶层 `CMakeLists.txt`。**例外（2026-07-12 对调，见 D8）**：本 PR 触及 cloud-agent-env 的两处既有资产——根 `sdkconfig.defaults`（blink 默认配置移入、P4 revision 移出）与 `AGENTS.md` 的 revision 声明位置（改指向 `sdkconfig.defaults.esp32p4`）；均为对调的必要连带，单一 P4 target 下不改变构建产物。

## 4. 设计决策（D1–D12，对应 grilling 与 review 问答，均已与用户确认）

- **D1（集成模式）**：采方案 A——融入 dev 单项目 `SRCS` 切换模式，而非在顶层新建独立 `blink/` 工程。理由：契合 dev 既定架构与 `AGENTS.md` 的 demo 切换约定，保持"根目录单工程 + `idf.py build` + CI 一致"，改动面最小；独立工程（方案 B）会破坏单项目定位并牵连 CI / 文档改造。
- **D2（LED 配置）**：沿用 main 的既定配置——LED strip（可寻址 RGB）+ GPIO51 + RMT backend（P4 支持 `SOC_RMT_SUPPORTED`，Kconfig 默认即 RMT）+ 周期 1000ms（Kconfig 默认）。理由：反映 WT9932P4-TINY 板载 RGB 硬件，main 分支已存在。
- **D3（依赖引入 + 版本）**：`led_strip` 用 managed component（新建 `main/idf_component.yml`），版本由 main 的 `^2.4.1` **升级到 `^3.0.3`**（当前最新版）。依赖名省略 `espressif/` 前缀、直接写 `led_strip`——ESP Component Registry 默认命名空间即 `espressif`，官方文档明确 `led_strip` 等价于 `espressif/led_strip`（与参考项目 ESP-Pocket2 的 `idf_component.yml` 写法一致）。兼容性核实（据实、留余地）：`3.0.3` 为当前最新版；**ESP-IDF v5.5.4 自带的 `get-started/blink` 示例（与本次移植同一份 `blink_example_main.c`）其 `idf_component.yml` 即声明 `espressif/led_strip: "^3.0.0"`**，佐证该 blink 源码与 3.x 兼容；CHANGELOG 显示 `3.0.0` 的 breaking 主要是"停止支持 ESP-IDF v4.x"（本项目 v5.5.4 不受影响），另一项"user-defined color component format"为新增可选配置。因此**预期**无需改源码即可编译，但这是基于官方示例与 CHANGELOG 的**推断**，最终以 `idf.py build` 的实际解析为准，不作绝对保证；`^3.0.3` 语义为 `>=3.0.3 <4.0.0`（未来可能解析到更新的 3.x；按用户决策**不锁定**版本，见 D12）。`managed_components/` 已被 `.gitignore` 忽略；`dependencies.lock` 不入库（不 `git add`；本 PR 不将其加入 `.gitignore`，为未跟踪文件）。
- **D4（命名约定）**：确立 `main_<demo>.c` 前缀约定。本次新增 `main_blink_example.c`；顺带把现有 `hello_world_main.c` 重命名为 `main_hello_world.c`（内容不变，`main/CMakeLists.txt` 的 `SRCS` 同步改名）。此为合理的顺带改进，属本次范围。
- **D5（`main.c`）**：保持原名 `main.c`（空 `app_main` 占位），视为"默认空入口"，豁免 `main_` 约定，仍以注释形式留在 `SRCS` 备选。
- **D6（默认 demo）**：默认激活 blink（`SRCS` 中 `main_blink_example.c` 取消注释、其余注释）。理由：blink 是本次新增主角，默认激活可让 `idf.py build` / CI 直接构建并覆盖 blink。
- **D7（Kconfig）**：原样移植 blink 的 `Kconfig.projbuild` 为新建的 `main/Kconfig.projbuild`（完整保留 LED 类型 choice / backend choice / `BLINK_GPIO` / `BLINK_PERIOD` 及 `orsource` env_caps）。零源码改动，`menuconfig` 全量可调。
- **D8（sdkconfig，2026-07-12 对调）**：blink 默认配置（`CONFIG_BLINK_LED_STRIP=y` + `CONFIG_BLINK_GPIO=51`）写入根 `sdkconfig.defaults`（它是"默认激活 demo"的默认值）；P4 revision 两项（`CONFIG_ESP32P4_SELECTS_REV_LESS_V3` / `CONFIG_ESP32P4_REV_MIN_0`）移入**新建**的 `sdkconfig.defaults.esp32p4`（附中文注释）。此布局让 `.esp32p4` 名实相符（只放 P4 target 专属符号），根 `sdkconfig.defaults` 承载"项目 target 选择（`CONFIG_IDF_TARGET`）+ 默认 demo 配置"。机制（据 IDF `kconfig.cmake` / `kconfgen/core.py` / `test_sdkconfig.py` 核实）：构建时 target 先由根 `sdkconfig.defaults` 的 `CONFIG_IDF_TARGET` 确定为 esp32p4；随后 Kconfig 生成阶段先取根 `sdkconfig.defaults`、再取 `.<target>` 变体作为输入（后者叠加）。**关键边界：`sdkconfig.defaults*` 只对"`sdkconfig` 中尚不存在的符号"生效**——kconfgen 先加载 defaults、再加载既有 `sdkconfig`（均 `replace=False`），故既有 `sdkconfig` 里已固化的符号会**压过** defaults；且 `idf.py fullclean` 只清 `build/`、不删 `sdkconfig`。因此全新 `sdkconfig`（CI / 全新 clone）首次生成即取根 `sdkconfig.defaults` 的 `BLINK_LED_STRIP=y`+`BLINK_GPIO=51`（`BLINK_*` 在最先加载的主 defaults 里首现即定）与 `.esp32p4` 的 revision 两项；但本地增量若已把 `BLINK_*` 的 Kconfig 内建默认（GPIO/8）落盘进 `sdkconfig`，须 `rm -f sdkconfig` 重新生成。plan 据此把 `Kconfig.projbuild` 与默认配置**合并到同一 task**（同时落位、一次构建），使 `BLINK_*` 首现即取默认；并在主验证前 `rm -f sdkconfig`、构建后正向断言 LED 模式。（历史：初版 D8 把 `BLINK_*` 放 `.esp32p4`、revision 留主 defaults，导致 `.esp32p4` 名义"target 专属"却实载"demo 专属"配置；2026-07-12 按用户 grilling 结论对调，使两文件名实相符。**单一 P4 target 下对调不改变任何构建产物**。）
- **D9（文档）**：只更新 `AGENTS.md`（不单独移植 blink 的 `README.md`）。在"选择运行哪个 demo"处补充：当前 demo 清单、默认激活 blink、`main_` 命名约定、blink 行为（板载 RGB：led_strip + GPIO51 + RMT + 1000ms）、依赖（`led_strip ^3.0.3` managed component）、`menuconfig` 可调项、以及 `sdkconfig.defaults` 屏蔽陷阱提示。理由：AGENTS.md 是 dev 唯一说明入口且已有对应章节，信息集中、不与项目级构建说明重复。
- **D10（交付 / Review 方式）**：spec/plan 阶段只写工作区文件、**不** `commit`/`push`/开 PR；用户直接查看工作区文件进行 Review。此为用户明确指令，优先于 Cloud Agent"每轮提交推送"的默认。**风险已暴露**：2026-07-10 环境重启导致未提交的 spec/plan 丢失、需重建；若后续仍担心丢失，可另行决定把 spec/plan 先提交到 feature 分支持久化（不影响 Review 流程）。
- **D11（PR / 提交约定，用于实施与最终交付）**：分支 `cursor/add-blink-example-6f89`（前缀 `cursor/`、后缀 `-6f89`、全小写）；PR → `dev`（实施完成后创建，初始为 draft、收尾后标记 ready for review；用 Cloud Agent 的 `ManagePullRequest` / `EditPullRequestLabels` 工具）；提交遵循 git cz + emoji、正文用 `-` 紧凑列出要点（提交信息内不留空行），scope 用主文件名/主题；最终合并为三个提交：① refactor(main_hello_world.c) 重命名、② feat(main_blink_example.c) blink 例程（源码+led_strip 依赖+Kconfig+默认配置+默认激活+AGENTS.md demo 说明）、③ docs(docs/superpowers) 文档（spec/plan）；标签 `enhancement`、指派 `yuangezhizao`；spec/plan 文档随实施阶段 PR 一起提交（放收尾的最后一次提交）。
- **D12（dependencies.lock，2026-07-12 反转）**：**不提交 `dependencies.lock`**（component manager 每次构建在项目根生成的依赖解析锁文件），即**不锁定** `led_strip` 解析版本；且**本 PR 不将其加入 `.gitignore`**（与 main 分支一致——main 既不提交 lock、也未 gitignore 它）。理由：用户明确要求"移除 `dependencies.lock`、不锁定版本"，并保持不 gitignore。取舍：放弃"锁定精确解析版本以保证跨环境完全可复现"（供应链最佳实践通常推荐 lockfile），换取"按 `^3.0.3` 语义始终取最新兼容 3.x"的行为；可复现性由 `.cursor/Dockerfile` 锁定的 ESP-IDF v5.5.4 与 `^3.0.3` 的兼容范围兜底。落地：构建生成的 lock 作为**未跟踪文件**出现在 `git status`，不 `git add` 即不入库；提交时用精确 `git add <path>`、勿 `git add -A` 误纳入。

## 5. 架构与组件（文件级变更清单）

**新建：**

- `main/main_blink_example.c`——blink 主程序，逐字取自 main 分支 `blink/main/blink_example_main.c`（`app_main` → `configure_led()` + 循环 `blink_led()` toggle）。单一职责：板载 RGB 闪烁 demo 的入口。
- `main/idf_component.yml`——声明 managed 依赖（乐鑫官方组件省略 `espressif/` 前缀；采用 component manager 标准模板，含 `idf: version: ">=5.5.0"` 约束与占位注释）。`idf>=5.5.0` 是本项目基线要求（Cloud/CI 固定 ESP-IDF v5.5.4，要求构建环境至少 5.5.0），不是 `led_strip 3.0.x` 自身最低要求：

```yaml
## IDF Component Manager Manifest File
dependencies:
  ## Required IDF version
  idf:
    version: ">=5.5.0"
  # Put list of dependencies here
  # For components maintained by Espressif:
  # component: "~1.0.0"
  led_strip: "^3.0.3"
  # For 3rd party components:
  # username/component: ">=1.0.0,<2.0.0"
  # username2/component2:
  #   version: "~1.0.0"
  #   # For transient dependencies `public` flag can be set.
  #   # `public` flag doesn't have an effect dependencies of the `main` component.
  #   # All dependencies of `main` are public by default.
  #   public: true
```

- `main/Kconfig.projbuild`——原样移植 blink Kconfig（`menu "Example Configuration"`：`orsource` env_caps + `BLINK_LED` choice(GPIO/LED strip) + `BLINK_LED_STRIP_BACKEND` choice(RMT/SPI) + `BLINK_GPIO` + `BLINK_PERIOD`）。
- `sdkconfig.defaults.esp32p4`（本 PR 新建）——P4 target 专属配置，承载既有的 P4 revision 两项（P4 构建时叠加于根 defaults 之上）：

```ini
# ESP32-P4 target 专属配置（P4 构建时叠加于 sdkconfig.defaults 之上）

# 适配 ESP32-P4 修订版本 <3.0（实测板卡为 revision v1.3）。ESP-IDF 默认按量产 v3.x 构建，若不声明本项，esptool 会以 "requires chip revision in range [v3.0 - v3.99] (this chip is revision v1.3)" 拒绝烧录。该选项在 ESP-IDF v5.5.4 与 latest 均存在；对 Cloud 内的纯构建无副作用，仅影响真实烧录时的芯片修订校验。CONFIG_ESP32P4_REV_MIN_0 对应 menuconfig 中 "Minimum Supported ESP32-P4 Revision = Rev v0.0"，为最低支持修订，向下兼容 0.x/1.x（含 v1.3）。
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_0=y
```

- `dependencies.lock`（仓库根，**不入库**）——component manager 构建后在项目根生成的解析锁文件；按 D12 **不提交**（不 `git add`、不锁定版本；本 PR 不将其加入 `.gitignore`，为未跟踪文件）。此处列出仅为说明其存在与处置，并非新增到 git 的文件。

**重命名：**

- `main/hello_world_main.c` → `main/main_hello_world.c`（内容不变）。

**修改：**

- `main/CMakeLists.txt`——`SRCS` 更新为三文件、默认激活 blink：

```cmake
idf_component_register(
    SRCS
    # "main.c"
    # "main_hello_world.c"
    "main_blink_example.c"

    INCLUDE_DIRS
    "."
)
```

- 根 `sdkconfig.defaults`——移入 blink 默认配置（`CONFIG_BLINK_LED_STRIP=y` + `CONFIG_BLINK_GPIO=51`）、移出 P4 revision 两项（迁至 `sdkconfig.defaults.esp32p4`）；`CONFIG_IDF_TARGET` 保留（见 D8 对调）。
- `AGENTS.md`——扩充"选择运行哪个 demo"章节（见 D9），并把 revision 声明位置同步为 `sdkconfig.defaults.esp32p4`。

**保持不变：**

- `main/main.c`（空占位）、根 `CMakeLists.txt`、根 `sdkconfig.defaults` 的 `CONFIG_IDF_TARGET`（一项）、CI、`.devcontainer`、`.vscode`、`fetch_repos.py`、`repos.json`。（注：根 `sdkconfig.defaults` 文件本身本次被修改——见"修改"区；此处仅指 `CONFIG_IDF_TARGET` 不动。P4 revision 两项已迁入 `sdkconfig.defaults.esp32p4`。）

**明确不移植（超出范围）：**

- main 分支的 `common_components/bsp_extra`（blink 的 `idf_component.yml` 只依赖 `led_strip`，不依赖它）。
- blink 的 `README.md`（改为并入 `AGENTS.md`）、`CMakeLists.txt`（dev 已有顶层工程 `CMakeLists.txt`）、`sdkconfig`（生成物，`.gitignore` 忽略）。
- 其它三个 demo（lvgl_demo_v8/v9、video_lcd_display）。

## 6. 控制流

**构建（`idf.py build`）：**

1. target 由根 `sdkconfig.defaults` 的 `CONFIG_IDF_TARGET="esp32p4"` 确定为 esp32p4（外加 revision 配置）。
2. component manager 读 `main/idf_component.yml`，把 `led_strip` 拉取到 `managed_components/`，并在项目根生成 `dependencies.lock`（记录解析版本；按 D12 不 `git add`、不入库；本 PR 不将其加入 `.gitignore`，为未跟踪文件）。
3. Kconfig 生成阶段按顺序把根 `sdkconfig.defaults`（含 `BLINK_LED_STRIP=y` + `BLINK_GPIO=51`）与其 target 变体 `sdkconfig.defaults.esp32p4`（含 P4 revision 两项）作为 defaults 输入（后者叠加）→ 两组配置分别生效（**仅当 `sdkconfig` 尚无这些符号时**，见 D8 边界；全新构建 / CI 满足，本地增量须先 `rm -f sdkconfig`）；`main/Kconfig.projbuild` 的 `orsource` 引入 P4 的 `env_caps`（`BLINK_GPIO` 合法范围 0–54）。
4. 编译 `SRCS` 激活的 `main_blink_example.c`（`#ifdef CONFIG_BLINK_LED_STRIP` 分支生效）+ 链接 `led_strip` → 产物 `build/WT9932P4-TINY.bin`。

**运行（板上）：** `app_main()` → `configure_led()` 用 `led_strip_new_rmt_device`（RMT backend、GPIO51、1 颗 LED）初始化 → `while(1)` 每 1000ms toggle：亮时 `led_strip_set_pixel(...,16,16,16)` + `refresh`，灭时 `led_strip_clear`，并 `ESP_LOGI` 打印 ON/OFF。

**切换 demo：** 编辑 `main/CMakeLists.txt` 的 `SRCS` 注释（在 `main_blink_example.c` / `main_hello_world.c` / `main.c` 间切换）后重新 `idf.py build`。

## 7. 错误处理与边界情况

- **`led_strip` 跨大版本（2.4.1 → 3.0.3）**：核心 API（`led_strip_new_rmt_device`/`set_pixel`/`refresh`/`clear`）稳定；ESP-IDF v5.5.4 自带 `get-started/blink` 示例即用 `led_strip ^3.0.0` 佐证兼容，`3.0.0` 的 breaking 仅"停止支持 IDF v4.x"（本项目 v5.5.4 满足）。故**预期**无需改源码即可编译，但以 plan 里的 `idf.py build` 为准；若真出现 API 不兼容（编译错误），按"发现偏差→汇报→更新计划"处理，不擅自改源码。
- **`sdkconfig.defaults` 屏蔽陷阱**（见 D8）：本地增量构建中残留的 `sdkconfig` 会压过后加入的 `sdkconfig.defaults*`，令 `BLINK_*` 停留在 Kconfig 内建默认（GPIO/8）而非板载 RGB（STRIP/51），且构建仍"成功"从而掩盖偏差。防护（已落到 plan）：`Kconfig.projbuild` 与默认配置合并到同一 task 引入（`BLINK_*` 首现即取默认，且置于最先加载的根 `sdkconfig.defaults`）；主验证 `rm -f sdkconfig` 干净重建；构建后正向断言 `CONFIG_BLINK_LED_STRIP=y` + `# CONFIG_BLINK_LED_GPIO is not set` + `CONFIG_BLINK_GPIO=51`。
- **依赖引入非"零副作用"**：`main/idf_component.yml` 一旦存在，即便 `SRCS` 仍是 `main_hello_world.c`，component manager 也会在构建时**拉取并把 `led_strip` 作为 main 组件依赖编译**（不是激活 blink 才发生）；它不参与 hello_world 的运行逻辑，但属**需构建验证**的依赖引入步骤，不能仅以"hello_world 不读取"断言其必然无害。
- **外部依赖（registry 网络）**：`led_strip` 是本仓库**首个 managed component**（此前无 managed 依赖、构建不联网）。引入后全新构建需能访问 ESP Component Registry（`components.espressif.com`）拉取 `led_strip`；egress 受限的环境会导致 Task 4 构建失败（首次拉取后缓存于 `managed_components/`）。
- **`dependencies.lock` 处理**：按 D12 **不提交**（不 `git add`）；本 PR 不将其加入 `.gitignore`，故构建生成后会作为**未跟踪文件**出现在 `git status`——提交时用精确 `git add`、勿用 `git add -A` 误纳入。
- **Kconfig `orsource` env_caps**：已验证 `$IDF_PATH/examples/common_components/env_caps/esp32p4/Kconfig.env_caps` 存在（P4：GPIO 0–54），默认 GPIO51 合法；env_caps 随 IDF 提供，无需复制进仓库。
- **A 模式配置常驻**：`led_strip` 依赖、`BLINK_*` 默认、`Kconfig.projbuild` 会常驻仓库；切回 hello_world 时它们仍在但不被引用/读取，属可接受的常驻冗余（已与用户确认接受）。
- **重命名一致性**：`hello_world_main.c` 重命名后必须同步更新 `main/CMakeLists.txt` 的 `SRCS`，避免引用旧文件名导致构建失败。
- **生成物勿入库**：`sdkconfig`、`managed_components/`、`dependencies/`、`build/` 均已被 `.gitignore` 忽略；`dependencies.lock` **未** gitignore（为未跟踪文件，见 D12）。提交时用精确 `git add`、避免 `git add -A` 误加生成物（尤其 `dependencies.lock`）。
- **未提交易失**：spec/plan 阶段不提交（D10）意味着环境重启会丢失（2026-07-10 已发生一次并重建）。

## 8. 测试与验证策略（实施阶段执行，本次不跑）

本仓库无单元测试 / lint 框架，验证以构建为主（对齐 CI）：

- **主验证**：默认 `SRCS`=blink，`rm -f sdkconfig && idf.py fullclean && idf.py build` 成功（删 `sdkconfig` 以镜像 CI 全新构建、规避配置屏蔽）——能拉取 `led_strip 3.0.x`、`main_blink_example.c` 编译链接通过、产出 `build/WT9932P4-TINY.bin`。
- **LED 模式正向断言**：构建后核对生成的 `sdkconfig` 含 `CONFIG_BLINK_LED_STRIP=y` + `# CONFIG_BLINK_LED_GPIO is not set` + `CONFIG_BLINK_GPIO=51`，确认走 led_strip 分支（防 GPIO 模式静默通过）。
- **依赖解析核对**：构建后可查看项目根生成的（未入库）`dependencies.lock` 中 `espressif/led_strip` 的 resolved version 为 `3.0.x`（`version` 通常在组件键后数行，`rg` 需带较大上下文如 `-A6`）；因不锁定版本，该值可能随上游发布而变化。
- **回归验证**：临时把 `SRCS` 切到 `main_hello_world.c` 构建成功（确认重命名无误），再复位为 blink 并再次构建成功。
- **体积报告**：`idf.py size`（与 CI 输出一致，供参考）。
- **CI**：合入后 CI 构建默认 demo（blink），并输出 `idf.py size` 与 `esptool image-info`。
- **硬件（可选，非 Cloud）**：真实烧录后观察板载 RGB 以约 1s 周期明暗交替。

## 9. 范围与非目标

- **范围**：仅 blink 一个 demo 的移植 + `main_` 命名约定（含 hello_world 重命名）+ 相关依赖/配置/文档改动。
- **非目标**：不移植 lvgl / video 等其它 demo；不移植 `common_components`；不改 CI、`.devcontainer`、`.vscode`、`fetch_repos.py`、`repos.json`、顶层 `CMakeLists.txt`。根 `sdkconfig.defaults` 仅保留 `CONFIG_IDF_TARGET`，P4 revision 两项按 D8 迁入 `sdkconfig.defaults.esp32p4`（对调的必要连带，不在既有项之外新增其它改动）。

## 10. QA（grilling 与 review 问答记录）

- **Q1 集成模式？** → A：融入 dev 单项目 `SRCS` 切换模式（不建独立 `blink/` 工程）。
- **Q2 LED 类型与 GPIO？** → 沿用 main：LED strip + GPIO51 + RMT backend + 1000ms 周期（板载 RGB）。
- **Q3 `led_strip` 引入方式？** → A：managed component（`main/idf_component.yml`）；并把版本从 `^2.4.1` 升级到 `^3.0.3`（IDF 自带 blink 用 `^3.0.0` 佐证兼容，最终以 build 为准）。
- **Q4 blink 源文件命名？** → 立 `main_` 约定：新增 `main_blink_example.c`，并把 `hello_world_main.c` 重命名为 `main_hello_world.c`。
- **Q5 空占位 `main.c` 如何处理？** → 保持 `main.c` 原名，仍作注释备选。
- **Q6 默认激活哪个 demo？** → (a) 默认激活 blink。
- **Q7 Kconfig 处理？** → (a) 原样移植 `Kconfig.projbuild`。
- **Q8 blink 默认配置放哪？** → 初为"新建 `sdkconfig.defaults.esp32p4` 承载 `BLINK_*`、根 defaults 两项不动"；**2026-07-12 对调**（见 D8）：`BLINK_*` 移入根 `sdkconfig.defaults`、P4 revision 两项移入 `sdkconfig.defaults.esp32p4`，使 `.esp32p4` 名实相符。
- **Q9 文档如何处理？** → (b) 只更新 `AGENTS.md`，不单独放 README。
- **Q10 spec/plan 的提交与 Review 方式？** → (b) 只写工作区、暂不提交，用户直接看文件 review。
- **Q11 交付约定？** → 确认：分支 `cursor/add-blink-example-6f89`、PR→`dev`（初始 draft，现已 ready for review）、git cz emoji + 逻辑拆分提交、label `enhancement` + 指派 `yuangezhizao`、spec/plan 随实施 PR 最后提交。
- **补充解释：`env_caps/Kconfig.env_caps` 是什么？** → ESP-IDF 官方示例用来"按目标芯片描述 GPIO 能力"的一小段 Kconfig（位于 `$IDF_PATH/examples/common_components/env_caps/<target>/`），定义 `ENV_GPIO_RANGE_MIN/MAX`、`ENV_GPIO_IN/OUT_RANGE_MAX` 等常量（P4 为 0–54，输入输出均到 54；对比 ESP32 为 0–39、输出仅到 33，因 GPIO34–39 仅输入）。blink 的 `config BLINK_GPIO ... range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX` 用它约束 `menuconfig` 里 GPIO 的合法范围，实现跨芯片可移植。`orsource`（optional + relative source）配合 `$IDF_TARGET` 自动引入当前芯片那份，文件缺失也不报错。
- **补充解释：RMT backend 是什么？** → RMT（Remote Control Transceiver，远程控制收发器）是 ESP32 系列的硬件外设，最初为红外遥控（NEC/RC5 等）设计，能把数据编码成一串可精确定时的脉冲（每个用「电平 + 持续时间」表示）经 GPIO 收发。因可生成任意精确时序，被广泛用于驱动 WS2812/SK6812 等可寻址 RGB LED（单线协议、数百 ns 级时序，普通 GPIO 翻转难以稳定满足）。blink 中 `led_strip` 的 RMT backend 即用 RMT 外设生成板载 RGB 所需的 WS2812 时序（对应源码 `led_strip_new_rmt_device`）；`Kconfig.projbuild` 的 `BLINK_LED_STRIP_BACKEND` choice 提供 RMT / SPI 两种 backend，二者择一。ESP32-P4 支持 RMT（`SOC_RMT_SUPPORTED`），故 Kconfig 默认在 P4 选 `BLINK_LED_STRIP_BACKEND_RMT`（本例即走此路径）。
- **Q12 关联 Plan 链接指向哪里？** → 指向 dev 分支的 GitHub 文档地址（`https://github.com/yuangezhizao/WT9932P4-TINY/blob/dev/docs/superpowers/plans/2026-07-07-blink-example.md`），与现有 cloud-agent-env spec 的写法一致。
- **Q13 依赖名是否带 `espressif/` 前缀？** → 省略前缀、只写 `led_strip`。ESP Component Registry 默认命名空间即 `espressif`，官方文档明确 `led_strip` 等价于 `espressif/led_strip`（README："For components maintained by Espressif only name can be used"），与 ESP-Pocket2 的 `idf_component.yml` 写法一致。
- **Q14 是否提交 `dependencies.lock`？** → 初为"是（提交以锁定版本）"，**2026-07-12 用户改为"否"**：不提交、不锁定版本，且**不将其加入 `.gitignore`**（与 main 分支一致——构建生成的 lock 为未跟踪文件，靠精确 `git add` 避免误提交）。详见 D12。
- **Q15 如何规避 `sdkconfig.defaults` 被既有 `sdkconfig` 屏蔽（导致 `BLINK_*` 停留在 GPIO/8）？** → 把 `main/Kconfig.projbuild` 与 blink 默认配置合并到同一任务（一次创建、一次构建），使 `BLINK_*` 符号首现即取默认（STRIP/51；对调后 `BLINK_*` 置于最先加载的根 `sdkconfig.defaults`）；主验证用 `rm -f sdkconfig && idf.py fullclean && idf.py build` 干净重建（`fullclean` 只清 `build/`、不删 `sdkconfig`）；构建后正向断言 `CONFIG_BLINK_LED_STRIP=y` + `# CONFIG_BLINK_LED_GPIO is not set` + `CONFIG_BLINK_GPIO=51`（防"GPIO8 普通 LED"静默通过）。机制边界记于 D8/§6/§7。
- **Q16 spec/plan 是否提交持久化？** → **分阶段**：设计阶段维持 D10（只写工作区、不 `commit`/`push`/开 PR，供 Review）；实施阶段随收尾提交入库（即 C3，见 R8/D11）。（相关：源文件命名 `main_blink_example.c` 保持不变，Q4 已定。）

## 11. 修订记录

- 2026-07-07：初稿（经 grilling 与用户逐项确认定稿）。
- 2026-07-07：关联 Plan 改为指向 dev 分支 GitHub 链接；`idf_component.yml` 依赖省略 `espressif/` 前缀写作 `led_strip`；补功能性需求 R8（新增文档需入库，时机依 D10/D11）。
- 2026-07-10：新增 D12（提交 `dependencies.lock` 锁定解析版本）；软化 `led_strip` 3.x 兼容表述并补 ESP-IDF 自带 blink `^3.0.0` 佐证；精确化"依赖引入非零副作用"与 `sdkconfig` 叠加机制表述；依赖核对改用 `dependencies.lock`。（注：环境重启曾致前一版未提交文档丢失，本版为重建。）
- 2026-07-11：修复 `sdkconfig.defaults` 屏蔽问题——把 `Kconfig.projbuild` 与 `sdkconfig.defaults.esp32p4` 合并到同一任务、主验证改用 `rm -f sdkconfig` 干净重建、增加 `BLINK_*` 正向断言；D8/§6/§7 补机制边界；D12/§8 补"lock 亦记录 idf 版本与 target"；lock 核查命令改 `rg -A6`；plan Task 1 Step 1 补"重启则先据对话重建"备注；任务数由 10 降为 9；确认不持久化；命名 `main_blink_example.c` 保持；补 registry 网络前置条件（§7）与 Task 1 的 `sdkconfig` 残留提示（`led_strip` 为首个 managed component）。
- 2026-07-12：把细粒度提交合并为三个并去正文空行；三提交 scope 定为 `refactor(main_hello_world.c)` / `feat(main_blink_example.c)` / `docs(docs/superpowers)`（C3 提交信息参考 ESP-Pocket2：路径式 scope + 分述 spec/plan）；实施完成后回填状态行、勾选复选框、新增「执行结果」。
- 2026-07-12：按 grilling 结论**对调** sdkconfig 布局——blink 默认配置移入根 `sdkconfig.defaults`、P4 revision 两项移入新建的 `sdkconfig.defaults.esp32p4`，使 `.esp32p4` 名实相符；推翻原"revision 两项不动"声明（D8/R5/N1/§5/§6/§7/Q8/Q15 同步）；单一 P4 target 下构建产物不变。
- 2026-07-12：`main/idf_component.yml` 改用 component manager 标准模板（`led_strip ^3.0.3` + `idf` 版本约束 + 占位注释）；RMT 详解仅置于本 spec QA（AGENTS.md / plan 改为指向）；AGENTS.md 新增按加入顺序排列的 demo 列表（`main.c` → `main_hello_world.c` → `main_blink_example.c`）。
- 2026-07-12：**`dependencies.lock` 最终决定**——不提交、不锁定版本、**不加入 `.gitignore`**（与 main 分支一致，构建生成的 lock 为未跟踪文件）；D12/R3/D3/§5/§6/§7/生成物/Q14 据此统一。
- 2026-07-12：同步 review 发现的文档不一致——§9 非目标删去"不改根 `sdkconfig.defaults` 既有两项"旧表述（与 D8 对调冲突）；Q16 改为"设计阶段不提交、实施随 C3 入库"（对齐 R8/状态行/C3）；§5 `sdkconfig.defaults.esp32p4` 代码块补全落盘的详细注释。
- 2026-07-12：PR #3 由 draft 标记为 ready for review——状态行、D11、Q11 的当前状态类表述同步为"创建时 draft、现已 ready for review"（创建步骤作历史保留）；PR 正文一并更新。
