# 新增板载 RGB blink 例程 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 main 分支的 blink 例程移植进 dev 的单项目 `SRCS` 切换脚手架，作为默认激活 demo 点亮板载 RGB（`led_strip` + GPIO51），并确立 `main_<demo>.c` 命名约定。

**Architecture:** dev 是"仓库根单一 `project()` + `main/CMakeLists.txt` 的 `SRCS` 注释切换 demo"。blink 以 `main_blink_example.c` 融入 `main/`，依赖 `led_strip` 用 managed component 引入（`dependencies.lock` 不入库、不锁定版本），Kconfig 落到 `main/Kconfig.projbuild`、blink 默认配置落到根 `sdkconfig.defaults`；P4 revision 两项置于 `sdkconfig.defaults.esp32p4`（构建时 target 变体叠加于根 defaults 之上）。

**Tech Stack:** ESP-IDF v5.5.4、ESP32-P4（RISC-V）、`led_strip ^3.0.3`（注册表全名 `espressif/led_strip`，RMT backend）、CMake、Kconfig。

**关联 Spec:** [`docs/superpowers/specs/2026-07-07-blink-example-design.md`](https://github.com/yuangezhizao/WT9932P4-TINY/blob/dev/docs/superpowers/specs/2026-07-07-blink-example-design.md)

---

## 执行前必读

- **本 plan 已于 2026-07-12 实施完成**（分支 `cursor/add-blink-example-6f89`，合并为 3 个提交 C1/C2/C3，PR #3 → `dev`；创建时为 draft，现已标记 ready for review）；以下步骤与复选框保留作实施记录（已勾选），执行详情见文末「执行结果」。
- **验证方式 = 构建**：本仓库无单元测试 / lint 框架（见 `AGENTS.md`「测试 / lint」）。因此每个任务的"验证"用 `idf.py build`（及必要时 `idf.py size`）代替单元测试，**不要自行发明单元测试**（超出本 plan 范围）。
- **工具约定**：下文出现的 `head`/`grep`/`cat`/`sed` 等只是**面向人类的等价示例**；agentic worker 请用编辑器的 `Read`/`Grep` 等工具完成同等查看/校验，避免 `cat/head/tail`、用 `rg` 代替 `grep`。
- **依赖引入非"零副作用"**：一旦创建 `main/idf_component.yml`，即使 `SRCS` 仍是 hello_world，component manager 也会在构建时拉取并编译 `led_strip`、并在项目根生成 `dependencies.lock`（本 PR 不 gitignore、也不入库，为未跟踪文件）。所以 Task 4 是**首次验证 component-manager + led_strip 解析/编译**的步骤，不要把它当成"理所当然绿色"。
- **构建需可达 ESP Component Registry（新增外部依赖）**：`led_strip` 是本仓库**首个 managed component**（此前 `repos.json=[]`、`main/` 无 `idf_component.yml`，构建从不联网）。自 Task 4 起，全新构建需能访问 `components.espressif.com` 拉取 `led_strip`（拉取后缓存于 `managed_components/`）；若某环境 egress 受限导致拉取失败 → 按"偏差处理"汇报。
- **`sdkconfig.defaults` 屏蔽陷阱（重要）**：`sdkconfig.defaults*` 只对"`sdkconfig` 中尚不存在的符号"生效（kconfgen 先加载 defaults、再加载既有 `sdkconfig`，后者压过前者），且 `idf.py fullclean` 只清 `build/`、**不删 `sdkconfig`**。为确保 `BLINK_LED_STRIP=y`+`BLINK_GPIO=51` 真正生效（而非停留在 Kconfig 内建默认 GPIO/8）：本 plan 把 `main/Kconfig.projbuild` 与 blink 默认配置**合并到同一个 Task 5**（同时落位、一次构建），使 `BLINK_*` 符号首次出现即取默认文件值（对调后 `BLINK_*` 写入最先加载的根 `sdkconfig.defaults`）；并在 Task 6 主验证前 `rm -f sdkconfig` 干净重建；构建后**正向断言** LED 走 strip 分支（非 GPIO 分支）。
- **`dependencies.lock` 不入库**：按 spec D12 **不提交**、不锁定版本；本 PR **不**将其加入 `.gitignore`（与 main 分支一致）——构建会在项目根生成它、作为**未跟踪文件**出现在 `git status`，提交时用精确 `git add`、勿 `git add -A` 误纳入。
- **提交约定**：git cz + emoji，正文用 `-` 紧凑列出要点（提交信息内不留空行），scope 用主文件名/主题；最终合并为三个提交——refactor(main_hello_world.c)（重命名）、feat(main_blink_example.c)（blink 例程：源码+依赖+Kconfig+默认配置+激活+AGENTS.md demo 说明）、docs(docs/superpowers)（spec/plan）。各 Task 为实现步骤，按此三组归并提交。
- **生成物勿入库**：`sdkconfig`、`managed_components/`、`dependencies/`、`build/` 均已被 `.gitignore` 忽略；`dependencies.lock` **未** gitignore（为未跟踪文件）。提交时用精确 `git add <path>`，不要 `git add -A`（尤其防误加 `dependencies.lock`）。
- **偏差处理**：若某步实际结果与预期不符（尤其 Task 5/6 的 `BLINK_*` 断言、blink 编译），按用户规则 STOP 并汇报"发现偏差：XXX，是否允许加入 plan？"，确认后再改。

---

## Task 1: 创建 feature 分支

**Files:** 无（仅 git 操作）

- [x] **Step 1: 确认起点干净并在 dev**

Run: `git -C /workspace status && git -C /workspace branch --show-current`
Expected: 工作区含未提交的 spec/plan 两个文档（其余干净）；当前分支 `dev`。
（注：spec/plan 按 D10 在设计阶段不持久化、仅存于工作区（Task 8 随 C3 入库）；若因环境重启导致其丢失，需先据本轮设计对话重建这两份文档再继续本 plan。）
（另注：`sdkconfig` 被 `.gitignore` 忽略、`git status` 看不到；若本地曾构建过、可能残留含 `CONFIG_BLINK_GPIO=8` 的旧 `sdkconfig`，为镜像全新环境请先 `rm -f /workspace/sdkconfig` 再开始。）

- [x] **Step 2: 拉取 main 分支（后续 Task 需从 origin/main 提取 blink 文件）**

Run: `git -C /workspace fetch origin main`
Expected: 成功，`origin/main` 可用。

- [x] **Step 3: 创建并切换到 feature 分支**

Run: `git -C /workspace checkout -b cursor/add-blink-example-6f89`
Expected: `Switched to a new branch 'cursor/add-blink-example-6f89'`。工作区里未提交的 spec/plan 随之带到新分支（先不提交，留到 Task 8）。

---

## Task 2: 重命名 hello_world 源文件，确立 main_ 命名约定

**Files:**
- Rename: `main/hello_world_main.c` → `main/main_hello_world.c`
- Modify: `main/CMakeLists.txt`

- [x] **Step 1: git 重命名（保留历史）**

Run: `git -C /workspace mv main/hello_world_main.c main/main_hello_world.c`
Expected: 无输出，`git status` 显示 `renamed: main/hello_world_main.c -> main/main_hello_world.c`。

- [x] **Step 2: 同步 `main/CMakeLists.txt` 的 SRCS（仍激活 hello_world，仅改文件名）**

把 `main/CMakeLists.txt` 内容改为：

```cmake
idf_component_register(
    SRCS
    # "main.c"
    "main_hello_world.c"

    INCLUDE_DIRS
    "."
)
```

- [x] **Step 3: 构建验证（hello_world 重命名后仍可编译）**

Run: `cd /workspace && idf.py build`
Expected: `Project build complete.`，产物 `build/WT9932P4-TINY.bin` 生成，无 "Cannot find source file" 报错。

- [x] **Step 4: 提交（即 C1，refactor(main_hello_world.c) 重命名）**

```bash
git -C /workspace add main/CMakeLists.txt main/main_hello_world.c
git -C /workspace commit -F - <<'EOF'
refactor(main_hello_world.c): ♻️ 重命名 hello_world 源文件并采用 main_ 命名约定

- git mv hello_world_main.c -> main_hello_world.c（内容不变）
- 同步 main/CMakeLists.txt 的 SRCS 引用
- 确立 main_<demo>.c 源文件命名约定
EOF
```

---

## Task 3: 新增 blink 源文件（暂不激活）

**Files:**
- Create: `main/main_blink_example.c`（逐字取自 main 分支 `blink/main/blink_example_main.c`，不改）

- [x] **Step 1: 从 origin/main 逐字提取源码到新文件名**

Run: `git -C /workspace show origin/main:blink/main/blink_example_main.c > /workspace/main/main_blink_example.c`
Expected: 生成 `main/main_blink_example.c`。校验：文件首行为 `/* Blink Example`；含 `led_strip` 相关调用（`led_strip_new_rmt_device`/`set_pixel`/`refresh`/`clear`）。（等价 shell：`head -1 ... && grep -c led_strip ...`）

> 说明：该源码用 `#ifdef CONFIG_BLINK_LED_STRIP` / `#elif CONFIG_BLINK_LED_GPIO` 两分支，`app_main()` 调 `configure_led()` 后 `while(1)` 每 `CONFIG_BLINK_PERIOD` ms toggle。与 spec「参考实现」一致，勿手改。

- [x] **Step 2: 构建验证（新文件未加入 SRCS，不参与编译；仍构建 hello_world）**

Run: `cd /workspace && idf.py build`
Expected: `Project build complete.`（`main_blink_example.c` 此时不被编译）。

- [x] **Step 3: 提交**

（不单独提交，随 Task 6 一并作为 C2 feat(main_blink_example.c) 提交）

---

## Task 4: 引入 led_strip 依赖（managed component）并验证解析

**Files:**
- Create: `main/idf_component.yml`
- 生成但**不提交**（不 `git add`；本 PR 不 gitignore，为未跟踪文件）: `dependencies.lock`（项目根）

- [x] **Step 1: 创建 `main/idf_component.yml`**

内容（component manager 标准模板；乐鑫官方组件省略 `espressif/` 前缀；含 `idf: ">=5.5.0"` 约束与占位注释。该约束是**项目基线要求**：本仓库 Cloud/CI 固定 ESP-IDF v5.5.4，要求构建环境至少 5.5.0；它比 `led_strip 3.0.x` 自身的最低 IDF 要求更严格）：

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

- [x] **Step 2: 构建验证——这是首次验证 component-manager + led_strip 解析/编译**

Run: `cd /workspace && idf.py build`
Expected: `Project build complete.`（此步会拉取并编译 `led_strip`——即便当前 `SRCS` 仍是 hello_world）。副产物：`managed_components/espressif__led_strip/` 出现；项目根生成 `dependencies.lock`（本 PR 不 gitignore、不入库，为未跟踪文件）。
（若此处因依赖解析/编译失败 → 按"偏差处理"STOP 汇报，不擅自绕过。）

- [x] **Step 3: 核对解析版本（以 `dependencies.lock` 为准，而非组件自身 manifest）**

读取项目根（构建生成、未入库的）`dependencies.lock`，确认 `espressif/led_strip` 的 `version` 解析为 `3.0.x`（lock 里 `version` 通常在组件键之后数行，需较大上下文才看得到）。（等价 shell：`rg -A6 "espressif/led_strip" /workspace/dependencies.lock`）

- [x] **Step 4: 提交（仅 `idf_component.yml` 清单；`dependencies.lock` 与 `managed_components/` 均不入库）**

（不单独提交，随 Task 6 一并作为 C2 feat(main_blink_example.c) 提交）

---

## Task 5: 新增 blink 的 Kconfig 与默认配置

> **合并说明**：`Kconfig.projbuild`（定义 `BLINK_*` 符号）与 blink 默认配置（给符号赋 `STRIP`/`51`，对调后写入根 `sdkconfig.defaults`）功能耦合，合并到**同一 Task / 同一提交**——一次性落位、再构建。这样 `BLINK_*` 符号首次出现时 defaults 已在，首次落盘即取 `STRIP`/`51`，规避「`sdkconfig.defaults` 屏蔽陷阱」（见执行前必读），且不产生 unknown-symbol 警告、也没有"defaults 引用未定义符号"的中间态。同一 Task 亦把 P4 revision 两项迁入新建的 `sdkconfig.defaults.esp32p4`（对调，见 spec D8）。

**Files:**
- Create: `main/Kconfig.projbuild`（逐字取自 main 分支 `blink/main/Kconfig.projbuild`）
- Modify: 根 `sdkconfig.defaults`（移入 blink 默认配置 `BLINK_LED_STRIP=y`+`BLINK_GPIO=51`、移出 P4 revision 两项）
- Create: `sdkconfig.defaults.esp32p4`（仓库根；承载迁入的 P4 revision 两项）

- [x] **Step 1: 从 origin/main 逐字提取 Kconfig**

Run: `git -C /workspace show origin/main:blink/main/Kconfig.projbuild > /workspace/main/Kconfig.projbuild`
Expected: 生成 `main/Kconfig.projbuild`。校验其内容含 `menu "Example Configuration"`、`orsource "$IDF_PATH/examples/common_components/env_caps/$IDF_TARGET/Kconfig.env_caps"`、`config BLINK_GPIO`、`config BLINK_PERIOD`、`choice BLINK_LED`、`choice BLINK_LED_STRIP_BACKEND`。供参考的期望内容：

```kconfig
menu "Example Configuration"

    orsource "$IDF_PATH/examples/common_components/env_caps/$IDF_TARGET/Kconfig.env_caps"

    choice BLINK_LED
        prompt "Blink LED type"
        default BLINK_LED_GPIO
        help
            Select the LED type. A normal level controlled LED or an addressable LED strip.
            The default selection is based on the Espressif DevKit boards.
            You can change the default selection according to your board.

        config BLINK_LED_GPIO
            bool "GPIO"
        config BLINK_LED_STRIP
            bool "LED strip"
    endchoice

    choice BLINK_LED_STRIP_BACKEND
        depends on BLINK_LED_STRIP
        prompt "LED strip backend peripheral"
        default BLINK_LED_STRIP_BACKEND_RMT if SOC_RMT_SUPPORTED
        default BLINK_LED_STRIP_BACKEND_SPI
        help
            Select the backend peripheral to drive the LED strip.

        config BLINK_LED_STRIP_BACKEND_RMT
            depends on SOC_RMT_SUPPORTED
            bool "RMT"
        config BLINK_LED_STRIP_BACKEND_SPI
            bool "SPI"
    endchoice

    config BLINK_GPIO
        int "Blink GPIO number"
        range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
        default 8
        help
            GPIO number (IOxx) to blink on and off the LED.
            Some GPIOs are used for other purposes (flash connections, etc.) and cannot be used to blink.

    config BLINK_PERIOD
        int "Blink period in ms"
        range 10 3600000
        default 1000
        help
            Define the blinking period in milliseconds.

endmenu
```

- [x] **Step 2: blink 默认配置写入根 `sdkconfig.defaults`；P4 revision 迁入新建的 `sdkconfig.defaults.esp32p4`（对调，见 spec D8）**

根 `sdkconfig.defaults` 的 blink 默认配置段（`CONFIG_IDF_TARGET` 之后）：

```ini
# 2) 默认激活 demo（blink）的默认配置（仅在激活 main_blink_example.c 时才有运行时意义；其它 demo 下这些符号仍写入生成的 sdkconfig，只是不被读取）：板载 RGB 走可寻址 led_strip + GPIO51。
CONFIG_BLINK_LED_STRIP=y
CONFIG_BLINK_GPIO=51
```

新建 `sdkconfig.defaults.esp32p4`（承载从主 defaults 迁出的 P4 revision 两项）：

```ini
# ESP32-P4 target 专属配置（P4 构建时叠加于 sdkconfig.defaults 之上）

# 适配 ESP32-P4 修订版本 <3.0（实测板卡为 revision v1.3）。ESP-IDF 默认按量产 v3.x 构建，若不声明本项，esptool 会以 "requires chip revision in range [v3.0 - v3.99] (this chip is revision v1.3)" 拒绝烧录。该选项在 ESP-IDF v5.5.4 与 latest 均存在；对 Cloud 内的纯构建无副作用，仅影响真实烧录时的芯片修订校验。CONFIG_ESP32P4_REV_MIN_0 对应 menuconfig 中 "Minimum Supported ESP32-P4 Revision = Rev v0.0"，为最低支持修订，向下兼容 0.x/1.x（含 v1.3）。
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_0=y
```

- [x] **Step 3: 构建验证 + 正向断言（BLINK_* 首现即取 defaults）**

Run: `cd /workspace && idf.py build`
Expected: `Project build complete.`，Kconfig 解析无 `orsource` 相关报错；因 `BLINK_*` 符号与 defaults 同时引入，**无 unknown-symbol 警告**。**正向断言**：核对生成的 `sdkconfig` 三项齐全：`CONFIG_BLINK_LED_STRIP=y`、`# CONFIG_BLINK_LED_GPIO is not set`、`CONFIG_BLINK_GPIO=51`（等价 shell：`rg "CONFIG_BLINK_LED|CONFIG_BLINK_GPIO" /workspace/sdkconfig`）。若出现 `CONFIG_BLINK_LED_GPIO=y` 或 `CONFIG_BLINK_GPIO=8` → 执行 `rm -f /workspace/sdkconfig && cd /workspace && idf.py build` 后再断言；仍不符则按"偏差处理"汇报。（`dependencies.lock` 不入库，无需核对。）

- [x] **Step 4: 提交（Kconfig + defaults 合并为一个逻辑提交）**

（不单独提交，随 Task 6 一并作为 C2 feat(main_blink_example.c) 提交）

---

## Task 6: 默认激活 blink 并做主验证

**Files:**
- Modify: `main/CMakeLists.txt`

- [x] **Step 1: 把 `SRCS` 切到 blink（其余注释）—— 记为「blink 版」**

把 `main/CMakeLists.txt` 内容改为（**blink 版**）：

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

- [x] **Step 2: 主验证——删 sdkconfig 干净重建 blink（镜像 CI 全新构建）**

Run: `cd /workspace && rm -f sdkconfig && idf.py fullclean && idf.py build`
（**用 `rm -f sdkconfig` 而非只 `fullclean`**：`fullclean` 只清 `build/`、不删 `sdkconfig`；删掉 `sdkconfig` 才能从 `sdkconfig.defaults*` 干净重生，与 CI / 全新 clone 行为一致，避免旧配置屏蔽 `BLINK_*`。）
Expected: `Project build complete.`，产物 `build/WT9932P4-TINY.bin` 生成；`main_blink_example.c` 与 `led_strip 3.0.x` 编译链接通过，无 API 不兼容报错。
**正向断言（防 GPIO 模式静默通过）**：核对生成的 `sdkconfig` 含 `CONFIG_BLINK_LED_STRIP=y` + `# CONFIG_BLINK_LED_GPIO is not set` + `CONFIG_BLINK_GPIO=51`（等价 shell：`rg "CONFIG_BLINK_LED|CONFIG_BLINK_GPIO" /workspace/sdkconfig`）；若显示 GPIO/8 → 未走 led_strip 分支，按"偏差处理"汇报。
（若出现 `led_strip` API 不兼容等编译错误 → 按"偏差处理"STOP 汇报，不擅自改源码。）

- [x] **Step 3: 体积报告**

Run: `cd /workspace && idf.py size`
Expected: 打印 Used static/DRAM/IRAM/Flash 汇总表，无错误。
（`dependencies.lock` 未 gitignore、不入库——构建后作为未跟踪文件出现在 `git status`，勿 `git add -A` 误纳入。）

- [x] **Step 4: 回归验证——确认 hello_world 仍可构建，再复位为 blink**

先把 `main/CMakeLists.txt` 临时改为（**hello_world 版**）：

```cmake
idf_component_register(
    SRCS
    # "main.c"
    "main_hello_world.c"
    # "main_blink_example.c"

    INCLUDE_DIRS
    "."
)
```

Run: `cd /workspace && idf.py build` → Expected: `Project build complete.`（确认重命名后的 hello_world 仍可编译）。

随后把 `main/CMakeLists.txt` **改回 Step 1 的「blink 版」** 并再次 `idf.py build` → Expected: `Project build complete.`。
提交前务必确认 `main/CMakeLists.txt` 最终为「blink 版」（`main_blink_example.c` 取消注释、其余注释）。本步不产生提交。

- [x] **Step 5: 提交（即 C2，feat(main_blink_example.c)；归并 Task 3/4/5/6/7 的全部 blink 文件）**

```bash
git -C /workspace add main/CMakeLists.txt main/main_blink_example.c main/idf_component.yml main/Kconfig.projbuild sdkconfig.defaults sdkconfig.defaults.esp32p4 AGENTS.md
git -C /workspace commit -F - <<'EOF'
feat(main_blink_example.c): ✨ 新增板载 RGB blink 例程

- 新增 main/main_blink_example.c（逐字取自 main 分支官方 Blink 示例）
- 引入 led_strip ^3.0.3 managed 依赖（main/idf_component.yml，声明 idf>=5.5.0）；dependencies.lock 不入库、不锁定版本（不 git add、不 gitignore）
- 移植 main/Kconfig.projbuild；blink 默认配置（CONFIG_BLINK_LED_STRIP=y + CONFIG_BLINK_GPIO=51）置于根 sdkconfig.defaults、P4 revision 两项迁入 sdkconfig.defaults.esp32p4
- main/CMakeLists.txt 默认激活 blink（SRCS）
- 更新 AGENTS.md：「选择运行哪个 demo」新增按加入顺序排列的 demo 列表，「Cloud Agent 环境」节同步 revision 声明指向 sdkconfig.defaults.esp32p4
EOF
```

---

## Task 7: 更新 AGENTS.md 的 demo 说明

**Files:**
- Modify: `AGENTS.md`（「选择运行哪个 demo」章节）

- [x] **Step 1: 替换该章节**

把以下现有内容：

```markdown
### 选择运行哪个 demo

实际生效的 `app_main` 由 `main/CMakeLists.txt` 中 `SRCS` 列表里未被注释的那个源文件决定；其余源文件以注释形式并列在同一处作为备选。切换 demo 只需在该 `SRCS` 中注释 / 取消注释对应文件并重新构建。
```

替换为：

```markdown
### 选择运行哪个 demo

实际生效的 `app_main` 由 `main/CMakeLists.txt` 中 `SRCS` 列表里未被注释的那个源文件决定；其余源文件以注释形式并列在同一处作为备选。切换 demo 只需在该 `SRCS` 中注释 / 取消注释对应文件并重新构建。

源文件采用 `main_<demo>.c` 命名约定（历史遗留的空白入口 `main.c` 除外）。当前可选 demo（按加入顺序，最新的在最后）：

- `main.c`：空 `app_main` 占位（最小基线）。
- `main_hello_world.c`：打印芯片信息与 "Hello world" 的基础示例。
- `main_blink_example.c`（**默认激活**）：点亮板载 RGB。源自 ESP-IDF 官方 Blink 示例，配置为可寻址 `led_strip`（GPIO51、RMT backend、周期 1000ms），依赖 `led_strip`（managed component，`main/idf_component.yml` 声明 `^3.0.3`，乐鑫官方组件省略 `espressif/` 前缀；不提交 `dependencies.lock`、不锁定解析版本）。可用 `idf.py menuconfig` 的 "Example Configuration" 调整 LED 类型 / backend / GPIO / 周期；默认配置见根 `sdkconfig.defaults`。注意：`sdkconfig.defaults*` 只对 `sdkconfig` 中尚不存在的符号生效，改了默认后若不生效需 `rm -f sdkconfig` 重新构建。（RMT backend 的详细说明见 `docs/superpowers` 下的 spec「补充解释：RMT backend 是什么？」。）
```

- [x] **Step 2: 提交**

（不单独提交；其 AGENTS.md 改动并入 C2，见 Task 6 Step 5 的 git add）

---

## Task 8: 提交文档（spec + plan，即 C3）

**Files:**
- Add: `docs/superpowers/specs/2026-07-07-blink-example-design.md`
- Add: `docs/superpowers/plans/2026-07-07-blink-example.md`

- [x] **Step 1: 提交两份 superpowers 文档（即 C3，docs(docs/superpowers)；仅归并 Task 8）**

```bash
git -C /workspace add docs/superpowers/specs/2026-07-07-blink-example-design.md docs/superpowers/plans/2026-07-07-blink-example.md
git -C /workspace commit -F - <<'EOF'
docs(docs/superpowers): 📝 新增 blink 例程 spec 与实现计划

- spec：blink 移植设计（融入 dev 单工程 SRCS 切换、默认激活；led_strip ^3.0.3 不锁定；sdkconfig.defaults 对调使 .esp32p4 名实相符），决策 D1-D12 / QA（含 RMT backend、sdkconfig 屏蔽陷阱补充解释）
- plan：Task 1-9 实现计划；勾选已完成步骤，回填执行结果（3 提交 / 构建验证 / 偏差处理 / 当前 Review 状态）
EOF

```

---

## Task 9: 收尾——推送与 PR（创建为 draft，收尾后标记 ready for review）

**Files:** 无（git / PR 操作）

- [x] **Step 1: 推送 feature 分支**

Run: `git -C /workspace push -u origin cursor/add-blink-example-6f89`
Expected: 分支推送成功。

- [x] **Step 2: 创建 draft PR → dev（用 Cloud Agent 的 PR 管理工具）**

用 `ManagePullRequest` 工具（`action=create_pr`）：`branch_name=cursor/add-blink-example-6f89`、`base_branch=dev`、`draft=true`、`title=feat(main_blink_example.c): ✨ 新增板载 RGB blink 例程`，`body` 概述本次改动（移植 blink、main_ 命名约定、led_strip ^3.0.3、不锁定版本、默认激活 blink、AGENTS.md 随 C2、spec/plan 随 C3）。若平台已自动为该分支建 PR，则改用 `action=update_pr` 完善标题/正文。

- [x] **Step 3: 加标签与指派**

用 `EditPullRequestLabels` 给该 PR 加标签 `enhancement`；指派 `yuangezhizao`。（若工具因仓库名大小写报 `PR URL must belong to the current repository`，把 `pr_url` 的仓库 slug 改为全小写 `wt9932p4-tiny` 重试——见 cloud-agent-env 记录的工具 quirk。）

---

## 执行结果（2026-07-12，实施完成）

按 superpowers:subagent-driven-development 执行（implementer 子代理逐 Task 实现 + spec 合规 / 代码质量两阶段 review + 终审代码审查），全部 Task 完成并通过构建验证；最终按用户要求把细粒度提交合并为 3 个：

- **C1** `refactor(main_hello_world.c)`：`git mv hello_world_main.c -> main_hello_world.c` + 同步 `main/CMakeLists.txt`（对应 Task 2）。
- **C2** `feat(main_blink_example.c)`：`main_blink_example.c`（逐字取自 main）+ `led_strip ^3.0.3`（`main/idf_component.yml`，声明 idf>=5.5.0；不提交 `dependencies.lock`、不 gitignore、不锁定）+ `main/Kconfig.projbuild` + blink 默认配置入根 `sdkconfig.defaults`、P4 revision 迁入 `sdkconfig.defaults.esp32p4` + `main/CMakeLists.txt` 默认激活 + `AGENTS.md` demo 说明（归并 Task 3/4/5/6/7）。
- **C3** `docs(docs/superpowers)`：本 spec/plan（归并 Task 8，含本次完成状态回填）。

**构建验证**：最终版本真实执行 `rm -f sdkconfig && idf.py build` 成功，产物 `build/WT9932P4-TINY.bin`（242608 B）；`led_strip 3.0.3` 与 blink 源码在 ESP-IDF v5.5.4 / ESP32-P4 下编译链接通过。正向断言全部命中：`CONFIG_BLINK_LED_STRIP=y` + `# CONFIG_BLINK_LED_GPIO is not set` + `CONFIG_BLINK_GPIO=51`（backend RMT）以及 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_0=y`；构建后 `git status -s` 仅 `?? dependencies.lock`（未跟踪、不入库、不 gitignore）。早期版本另做过 `fullclean` 与 hello_world 回归构建，最终文档以最新验证命令为准。

**执行中的偏差与处理**：
- 提交粒度：初版按 Task 拆成 7 个细粒度提交，后按用户要求合并为 3 个（C1/C2/C3）并 `--force-with-lease` 重写历史；树内容除本 spec/plan 外与原实现逐字节一致（交叉校验通过）。
- 提交信息：初版用多个 `-m` 致正文各要点间有空行，改为单次多行正文、无空行（用 `-F`/`-m` 一次性写入，与 Task 2 / Task 6 / Task 8 的 heredoc 示例及真实提交一致）。
- 提交 scope、依赖锁定与文档归属：按用户要求把 3 提交 scope 改为 `refactor(main_hello_world.c)` / `feat(main_blink_example.c)` / `docs(docs/superpowers)`；`dependencies.lock` 最终为不提交、不锁定、不加入 `.gitignore`；`AGENTS.md` 从 C3 移入 C2，C3 仅保留 spec/plan；多轮历史重写均用 `--force-with-lease` 推送。

**最终 Review 状态**：当前版本已针对多轮 review 修复阻断/重要文档不一致；PR 标题已与 C2 scope 统一为 `feat(main_blink_example.c)`；仍保留的取舍为 `dependencies.lock` 不提交且不 gitignore、`idf_component.yml` 显式要求 `idf>=5.5.0`（与本仓库 ESP-IDF v5.5.4 基线一致）。

**交付**：PR #3 → `dev`（创建时 draft，现已标记 ready for review），label `enhancement`，指派 `yuangezhizao`。

## Self-Review（写完计划后自查，已完成）

- **Spec 覆盖**：R1(Task 3/6)、R2(Task 6)、R3(Task 4，`led_strip` 依赖、`dependencies.lock` 不入库)、R4(Task 5，Kconfig)、R5(Task 5，blink 默认配置入根 `sdkconfig.defaults`、P4 revision 入 `sdkconfig.defaults.esp32p4`)、R6(Task 2)、R7(Task 7)、R8(Task 8)；N1(Task 2/6 回归)、N2(Task 4/5/6)、N3(Task 6)、N4(仅动清单内文件；2026-07-12 对调后额外触及 cloud-agent-env 的根 `sdkconfig.defaults` 与 `AGENTS.md` revision 位置，见 D8)。D1–D12 均落到对应任务（D12→Task 4 不提交 lock（不 gitignore、未跟踪）；D8 对调→Task 5 blink 默认配置入主 defaults、revision 入 `.esp32p4` + Task 6 `rm -f sdkconfig` + 正向断言）。
- **占位符**：无 TBD/TODO；配置文件给出完整内容，逐字文件用 `git show` 精确提取。
- **类型/命名一致性**：`main_blink_example.c` / `main_hello_world.c` / `main.c`、`CONFIG_BLINK_LED_STRIP` / `CONFIG_BLINK_GPIO`、`led_strip ^3.0.3`、分支 `cursor/add-blink-example-6f89` 在各任务间一致。
- **提交分组**：最终合并为 3 个提交（C1/C2/C3）——C1 refactor(main_hello_world.c) 重命名（Task 2）、C2 feat(main_blink_example.c) blink 例程（Task 3/4/5/6/7 归并）、C3 docs(docs/superpowers) 文档（Task 8 归并）；提交信息正文用 `-` 紧凑列出、无空行。

## 修订记录

- 2026-07-07：初稿（基于定稿 spec 编写，待批准后执行）。
- 2026-07-10：Task 4 增加提交 `dependencies.lock` 与以 lock 核对解析版本；修正"依赖引入非零副作用"表述；给出明确的回归 patch 与复位校验；明确用 Cloud Agent 的 PR 管理工具；补充工具约定说明。（注：环境重启曾致前一版未提交文档丢失，本版为重建。）
- 2026-07-11：修复 `sdkconfig.defaults` 屏蔽问题——把 `Kconfig.projbuild` 与 `sdkconfig.defaults.esp32p4` 合并为单个 Task 5（同时创建、一次构建、一个提交）、主验证改用 `rm -f sdkconfig && idf.py fullclean && idf.py build` 干净重建、增加 `BLINK_*` 正向断言、执行前必读补「屏蔽陷阱」、lock 核查命令改 `rg -A6`；Task 1 Step 1 补"重启则先据对话重建"备注；任务总数由 10 降为 9；确认不持久化；执行前必读补 registry 网络前置条件、Task 1 Step 1 补 `sdkconfig` 残留提示。
- 2026-07-12：提交合并为三个（scope 定为 `refactor(main_hello_world.c)` / `feat(main_blink_example.c)` / `docs(docs/superpowers)`）、正文去空行；执行前必读、各 Task 提交步骤、Self-Review 同步；实施完成后回填状态行、勾选复选框、新增「执行结果」。
- 2026-07-12：按 grilling 结论对调 sdkconfig 布局（blink 默认配置入根 `sdkconfig.defaults`、P4 revision 入 `sdkconfig.defaults.esp32p4`）；Architecture、Task 5、Task 6、执行结果、Self-Review 同步。
- 2026-07-12：`main/idf_component.yml` 改用 component manager 标准模板；RMT 详解移至 spec QA（本 plan demo 展示改为指向）；AGENTS.md 与本 plan 新增按加入顺序排列的 demo 列表（AGENTS.md 随 C2，plan 展示随 C3）。
- 2026-07-12：**`dependencies.lock` 最终决定**——不提交、不锁定、**不加入 `.gitignore`**（与 main 分支一致，未跟踪文件）；lock 措辞据此统一（含此前遗漏的 spec §6）。
- 2026-07-12：同步 review 发现的文档不一致——Task 4 标题去掉"并提交 lock"（与 D12 一致）；Task 5 `sdkconfig.defaults.esp32p4` 代码块与 `sdkconfig.defaults` 注释对齐落盘文件；Task 6 Step 5 `git add` 补 `AGENTS.md`、示例正文对齐真实 C2（AGENTS.md「新增」demo 列表）、归并范围记为 Task 3/4/5/6/7；Task 7 Step 2 说明改为并入 C2；Task 8 示例措辞与真实 C3 对齐；Tech Stack 的 `led_strip` 注明注册表全名。
- 2026-07-12：复审 cosmetic 项修正——Task 5 Step 2 blink 段补回 `# 2)` 段序前缀（对齐落盘 sdkconfig.defaults）；Task 6 Step 5 示例第 2 条补"（不 git add、不 gitignore）"、第 5 条对齐真实 C2 的 AGENTS.md revision 同步措辞；Task 1 Step 1 备注补"设计阶段 / Task 8 随 C3 入库"限定。C2 提交信息补 AGENTS.md revision 声明同步、PR 正文 revision 归属小节表述一并修正。
- 2026-07-12：统一 plan 提交示例写法——Task 2 Step 4（C1）与 Task 6 Step 5（C2）的 `git commit -m` 示例改为 `-F - <<'EOF'` heredoc（subject 与 body 间补空行），与 Task 8（C3）写法及真实提交（subject + 空行 + body；`Co-authored-by` 为自动 trailer）一致，修复复审发现的"示例缺 subject/body 空行"。
- 2026-07-12：PR #3 由 draft 标记为 ready for review——执行前必读、交付同步为当前 ready 状态（Task 9 标题保留"创建 draft→收尾 ready"的过程性表述、其创建步骤作历史保留）；「执行结果·偏差处理」的 heredoc 等价说明改为并列 Task 2/6/8；PR 正文一并更新。
