# WT9932P4-TINY Cloud Agent 环境（Dockerfile 化）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: 使用 superpowers:executing-plans 或 superpowers:subagent-driven-development 逐任务实施本计划（二者对比与本任务建议见第四节）。步骤用 `- [ ]` 复选框跟踪。

**Spec:** [`docs/superpowers/specs/2026-07-05-cloud-agent-env-design.md`](https://github.com/yuangezhizao/WT9932P4-TINY/blob/dev/docs/superpowers/specs/2026-07-05-cloud-agent-env-design.md)（先读 spec 了解「是什么/为什么」，本 plan 给出「怎么做」的逐步实现；链接指向合并后的 `dev` 分支路径）。

**Goal:** 参考 `yuangezhizao/ESP-Pocket2` PR #1，把 WT9932P4-TINY 的 Cloud Agent 开发环境「配置即代码」（Dockerfile 模式），并让全新环境 `idf.py build` 对 ESP32-P4（含 revision v1.3 芯片）开箱即用；以 draft PR 形式合入 `dev`。

**Architecture:** 新增 `.cursor/environment.json`（Dockerfile 模式，`install` 用 `fetch_repos.py` 而非 submodule）+ `.cursor/Dockerfile`（`FROM espressif/idf:v5.5.4@sha256:b9f2d6ea…`，tag+digest 双锁定）+ `sdkconfig.defaults`（固定 esp32p4 与 revision）+ `AGENTS.md`（适配 P4/依赖机制/QEMU 现状）。`.devcontainer/` 保持不动。

**Tech Stack:** Cursor Cloud Agent（`.cursor/environment.json` Dockerfile 模式）、Espressif 官方镜像 `espressif/idf:v5.5.4`、ESP-IDF v5.5.4、ESP32-P4（RISC-V）、`fetch_repos.py`。

---

## 一、文件结构（创建/修改）

- 创建 `.cursor/environment.json` — Cloud Agent 运行配置（Dockerfile 模式 + `install` 拉依赖）。
- 创建 `.cursor/Dockerfile` — Cloud Agent 镜像定义（`FROM espressif/idf:v5.5.4@sha256:b9f2d6ea…` tag+digest 双锁定 + 补装 CLI）。
- 创建 `sdkconfig.defaults` — 固定 esp32p4 目标与 revision（开箱即用 + 适配 v1.3）。
- 创建 `AGENTS.md` — 面向人/agent 的环境与构建说明（适配 P4/`fetch_repos.py`/QEMU 现状）。
- 不改：`.devcontainer/`（ESP-IDF 自动生成，沿用一贯意见）、`CMakeLists.txt`、`main/CMakeLists.txt`、CI、`.vscode/`、`fetch_repos.py`、`repos.json`。

> 前置检查：`.gitignore` 已忽略生成物 `sdkconfig`（第 33 行）但**不**忽略 `sdkconfig.defaults`，可安全提交；`dependencies/` 已被忽略。

---

## 二、任务分解

> 执行顺序：**先 Task 0 建分支**（避免在错误分支产生改动），再 Task 1–4 创建文件，然后 Task 5 提交、Task 6 建 PR、Task 7 新会话验证。

### Task 0：基于最新 `dev` 建分支（必须最先执行）

**Files:** 无，仅 `git`。

- [x] **Step 1：确认工作区并从最新 `dev` 建分支**

```bash
git status --porcelain           # 确认工作区：应仅有未跟踪的 docs/superpowers/...（plan/spec），无其他误改
git fetch origin                 # 刷新 origin/* 远程跟踪分支（确保 origin/dev 为最新；`git fetch origin dev` 可能只更新 FETCH_HEAD）
git checkout -b cursor/cloud-agent-env-5858 origin/dev
```
Expected: 位于新分支 `cursor/cloud-agent-env-5858`，且 `git rev-parse HEAD` 与 `git rev-parse origin/dev` 相同（分支基于最新 `dev`，天然包含已合并的 PR #1）；**不写死具体 commit hash**（`dev` 可能已前进）。

> 注意：本会话工作区可能有未跟踪的 `docs/superpowers/...`（plan/spec），`git checkout -b` 会带到新分支，属预期（它们将在 Task 5 Step 6 作为最后一个提交）；确认没有其他误改即可。

---

### Task 1：创建 `sdkconfig.defaults`（固定 esp32p4 + revision）

**Files:**
- Create: `sdkconfig.defaults`

- [x] **Step 1：写入文件内容**

```ini
# WT9932P4-TINY 工程默认配置（idf.py 首次构建 / 重置 sdkconfig 时读取）

# 1) 固定构建目标为 ESP32-P4：保证全新环境（无 sdkconfig）执行 idf.py build 开箱即用、不回退到默认 esp32 目标，并与 CI（.github/workflows/build-esp-idf-project.yml 的 ESP_IDF_TARGET: ESP32P4）一致。
CONFIG_IDF_TARGET="esp32p4"

# 2) 适配 ESP32-P4 修订版本 <3.0（实测板卡为 revision v1.3）。ESP-IDF 默认按量产 v3.x 构建，若不声明本项，esptool 会以 "requires chip revision in range [v3.0 - v3.99] (this chip is revision v1.3)" 拒绝烧录。该选项在 ESP-IDF v5.5.4 与 latest 均存在；对 Cloud 内的纯构建无副作用，仅影响真实烧录时的芯片修订校验。CONFIG_ESP32P4_REV_MIN_0 对应 menuconfig 中 "Minimum Supported ESP32-P4 Revision = Rev v0.0"，为最低支持修订，向下兼容 0.x/1.x（含 v1.3）。
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_0=y
```

- [x] **Step 2：本地静态校验（不产生 sdkconfig）**

Run: `test -f sdkconfig.defaults && grep -qx 'CONFIG_IDF_TARGET="esp32p4"' sdkconfig.defaults && grep -qx 'CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y' sdkconfig.defaults && grep -qx 'CONFIG_ESP32P4_REV_MIN_0=y' sdkconfig.defaults && echo OK`
Expected: 打印 `OK`——用 `grep -qx` 精确整行匹配三条配置（校验具体值而不仅是键名；能发现 target 被写错或缺项的情况，也不受注释干扰）。

> 说明：不在本步执行 `idf.py build`（当前会话 IDF 为 5.4 快照，非目标镜像；真正构建验证放到 Task 7 的新 Cloud Agent）。

---

### Task 2：创建 `.cursor/Dockerfile`

**Files:**
- Create: `.cursor/Dockerfile`

- [x] **Step 1：写入文件内容**

```dockerfile
# WT9932P4-TINY Cloud Agent 环境镜像
#
# 目标：把开发环境「代码化」，不依赖任何人的个人快照（Personal snapshot），任何人从带本文件的分支起 Cloud Agent 都能得到一致、可复现的环境。
#
# 说明：
# - Cloud Agent 本身就运行在「一个容器」里（Firecracker microVM 内的容器）；本 Dockerfile 只是「声明这个容器长什么样」，并非 docker-in-docker。未来要装 Tailscale / cloudflared，直接在本文件里 apt 安装或下载二进制、运行时按官方 userspace 方式启动即可，全程无需 docker-in-docker。
# - 基于 Espressif 官方镜像 espressif/idf:v5.5.4：已内置 ESP-IDF v5.5.4、RISC-V 交叉编译器（riscv32-esp-elf）、Python venv；IDF_PATH=/opt/esp/idf，IDF_TOOLS_PATH=/opt/esp。
# - 官方镜像是 Ubuntu based，满足 Cursor computer use（Desktop）对 Debian/Ubuntu 的要求。
# - 不要 COPY 项目源码：Cursor 会自行 checkout 正确的 commit 到工作区。
# - 本项目芯片为 ESP32-P4（RISC-V），与 ESP-Pocket2(ESP32-S3/Xtensa) 不同。实测本镜像已自带 libslirp0 与 qemu-system-riscv32/xtensa（见 /opt/esp/tools/qemu-*），故本 apt 块不重复安装；但 ESP32-P4 的 idf.py qemu 官方尚未支持（work in progress、未文档化），因此 Cloud 内仅做 idf.py build 构建验证（详见 AGENTS.md）。
# - tag+digest 双锁定：tag（v5.5.4）便于人读、digest（@sha256:…）保证字节级不可变（同一 tag 可被重推、digest 不会）。更新 IDF 版本时需同步更新 digest；digest 获取方式：docker buildx imagetools inspect espressif/idf:<tag>（取 index digest）。
FROM espressif/idf:v5.5.4@sha256:b9f2d6ea1c19e0c9f7959bdb74a9e3c775642f9d0f3b841937c5fa3363db892b

# ┌───────────────────────────────────────────────────────────────────────────┐
# │ 平台会「自动安装」的包（多数无需重复安装；sudo 例外，见下方 apt 块）                              │
# │                                                                            │
# │ Cursor 每次启动都会跑桌面初始化，按容器内                                   │
# │   /usr/local/share/vnc-desktop.Aptfile                                     │
# │ 用 apt-get install --no-install-recommends 安装下面这些（实测清单）：       │
# │                                                                            │
# │   VNC     : tigervnc-standalone-server tigervnc-common tigervnc-tools      │
# │   XFCE    : xfce4 xfce4-terminal xfce4-settings thunar                     │
# │   X11     : x11-utils x11-xserver-utils xdg-utils xdotool xclip procps     │
# │   D-Bus   : dbus-x11 at-spi2-core                                          │
# │   应用    : mousepad seahorse sudo ffmpeg                                  │
# │   主题    : adwaita-icon-theme gnome-themes-extra gnome-keyring plank      │
# │             sassc libglib2.0-dev-bin libxml2-utils dconf-cli xz-utils      │
# │   locale  : locales                                                       │
# │   字体    : fonts-noto fonts-wqy-microhei fonts-droid-fallback             │
# │             fonts-noto-color-emoji fonts-liberation fonts-croscore         │
# │             fonts-cantarell fonts-jetbrains-mono xfonts-base xfonts-terminus│
# │   桌面库  : libx11-dev libxkbfile-dev libsecret-1-dev libgbm-dev           │
# │             libnotify4 libnss3 libxss1 libgl1-mesa-dri libglx-mesa0 libgl1 │
# │   Python  : python3-minimal python3-numpy                                  │
# │   资产依赖: bash ca-certificates coreutils curl findutils gzip tar         │
# │   浏览器  : google-chrome-stable（单独脚本 install-google-chrome 安装）     │
# │                                                                            │
# │ 另外平台还会：把 gh 软链到 /usr/local/bin/gh（→/exec-daemon/gh）、         │
# │ 配置 git 身份与 SSH 提交签名、每次启动 git clean -fd 并重建                │
# │ /opt/cursor/artifacts。                                                    │
# │                                                                            │
# │ ⚠️ 维护提示（上述清单实测于 2026-07-05；实测 sha256=819987b7… 与平台      │
# │    /usr/local/share/vnc-desktop.Aptfile.version 一致，平台清单可能随时间   │
# │    变化）：若将来 Cursor 不再自动装上述包（表现为桌面/中文异常），          │
# │    把缺的项补进下面的 apt-get install。                                    │
# │    （相比 ESP-Pocket2 初版，新增了「资产依赖」一节：bash/ca-certificates/  │
# │     coreutils/curl/findutils/gzip/tar。）                                  │
# └───────────────────────────────────────────────────────────────────────────┘

# 只安装「基础镜像实测缺失」的包（2026-07-05 于 espressif/idf:v5.5.4 容器实测）：
# - sudo       : Cursor 官方要求 Dockerfile 具备 git+sudo；实测镜像已含 git、缺 sudo，故补 sudo
# - 开发 CLI   : htop vim file lsof jq net-tools iproute2 dnsutils（实测镜像均无）
# - dfu-util   : 设备烧录（实测镜像无）
# 实测镜像已自带、故不重复安装：git、less、ccache、git-lfs、libslirp0、qemu-system-xtensa/riscv32。
RUN apt-get update && apt-get install -y --no-install-recommends \
      sudo \
      htop vim file lsof jq \
      net-tools iproute2 dnsutils \
      dfu-util \
    && rm -rf /var/lib/apt/lists/*

# 让交互式（非登录）shell 自动进入 ESP-IDF 环境（agent 的 shell 未必经过镜像 ENTRYPOINT），等价于快照环境里 ~/.bashrc 中 source export.sh 的效果。
# 注意：/etc/bash.bashrc 仅对交互式 bash 生效；非交互脚本（如 bash -c、CI）不读取它，需要时请显式 source。
RUN echo '. /opt/esp/idf/export.sh >/dev/null 2>&1 || true' >> /etc/bash.bashrc
```

- [x] **Step 2：校验**

Run: `test -f .cursor/Dockerfile && grep -qx 'FROM espressif/idf:v5.5.4@sha256:b9f2d6ea1c19e0c9f7959bdb74a9e3c775642f9d0f3b841937c5fa3363db892b' .cursor/Dockerfile && if awk '/^RUN apt-get update && apt-get install/,/rm -rf/' .cursor/Dockerfile | grep -qE 'libslirp0|less|ccache|git-lfs'; then echo 'FAIL: 镜像已自带的包不应在 apt block'; exit 1; else echo OK; fi`
Expected: 打印 `OK`——`FROM espressif/idf:v5.5.4@sha256:b9f2d6ea…`（tag+digest 双锁定）整行精确存在（`grep -qx` 匹配真实 `FROM` 行、非注释），且真正的 `RUN apt-get … install` 块内不含**实测镜像已自带**的 `libslirp0/less/ccache/git-lfs`（用 `^RUN` 锚定安装块起点，避免匹配注释里的 `apt-get install`；命中任一则 `exit 1`，防止把冗余包加回）。

---

### Task 3：创建 `.cursor/environment.json`

**Files:**
- Create: `.cursor/environment.json`

- [x] **Step 1：写入文件内容**

```json
{
  "build": {
    "dockerfile": "Dockerfile",
    "context": ".."
  },
  "install": "python3 fetch_repos.py --yes"
}
```

> 依据（Cursor 官方文档）：`build.dockerfile`/`build.context` 相对 `.cursor/` 目录（`Dockerfile`→`.cursor/Dockerfile`，`..`→仓库根）；`install`（= `update`）在 checkout 后**同步阻塞**运行、需**幂等**、耗时>数秒会被快照缓存。`fetch_repos.py` 用 `git fetch`+`reset --hard` 对齐远程，幂等；`--yes` 跳过交互；当前 `repos.json` 为 `[]`，脚本直接跳过。故无需 `start`/`terminals`。

- [x] **Step 2：校验 JSON 合法**

Run: `python3 -c "import json;d=json.load(open('.cursor/environment.json'));print(d['build']['dockerfile'], d['build']['context'], d['install'])"`
Expected: 打印 `Dockerfile .. python3 fetch_repos.py --yes`。

---

### Task 4：创建 `AGENTS.md`

**Files:**
- Create: `AGENTS.md`

- [x] **Step 1：写入文件内容**

```markdown
# AGENTS

## 交互约定

- 与本仓库交互时，请始终使用中文回复（代码、路径、命令除外）。

## Cursor Cloud specific instructions

> 说明：保留该英文章节标题作为工具约定锚点；下方内容使用中文。

本仓库是 **WT9932P4-TINY**，一个面向 **ESP32-P4**（RISC-V）芯片的 ESP-IDF（v5.5.4）固件项目。没有 host 应用或 web 服务——构建产物是运行在 ESP32-P4 上的固件。

### Cloud Agent 环境（Dockerfile 模式，配置即代码）

- 环境由仓库内 **`.cursor/environment.json` + `.cursor/Dockerfile`** 定义，基于官方镜像 **`espressif/idf:v5.5.4`**（Dockerfile 中 tag+digest 双锁定），不依赖任何个人快照。
- 解析优先级：仓库 `.cursor/environment.json` > 个人 saved environment > 团队 saved environment。因此从带本配置的分支起 Cloud Agent 会自动使用本 Dockerfile：无需在 dashboard 手动创建环境，也无需删除已有的个人快照（它会被更高优先级的仓库配置覆盖，仅作 fallback）。
- ESP-IDF 位于 **`/opt/esp/idf`**（`IDF_PATH`），工具链在 `/opt/esp`。`export.sh` 已在镜像的 `/etc/bash.bashrc` 自动 source，新 shell 可直接用 `idf.py`；若某个 shell 没有该命令，运行 `source /opt/esp/idf/export.sh`（或官方 alias `get_idf`）。
- 构建目标 `esp32p4` 由 `sdkconfig.defaults`（`CONFIG_IDF_TARGET="esp32p4"`）声明式固定：全新环境首次 `idf.py build` 会自动选中 esp32p4，无需手动 `set-target`。实际 `sdkconfig` 由其生成，不应提交（已在 .gitignore 忽略，注意勿用 `git add -A` 误加入）。
- `sdkconfig.defaults` 另声明 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` 与 `CONFIG_ESP32P4_REV_MIN_0=y`，用于适配 revision <3.0 的样片（实测板为 v1.3，最低支持修订 Rev v0.0）；这只影响真实烧录时的芯片修订校验，不影响 Cloud 内的纯构建。**注意 ESP32-P4 的 rev <3.0 与 rev ≥3.0 互斥**：本配置面向 <3.0 工程样片，若改用 rev ≥3.0 量产芯片，需相应切换 revision 配置（去掉 `SELECTS_REV_LESS_V3`）。
- 依赖组件放在 **`dependencies/`** 目录（`CMakeLists.txt` 里 `EXTRA_COMPONENT_DIRS dependencies`），由 **`fetch_repos.py`**（读取 `repos.json`）管理，而非 git submodule。`environment.json` 的 `install` 每次启动执行 `python3 fetch_repos.py --yes` 拉齐它们（当前 `repos.json` 为空 `[]`，脚本会直接跳过）。
- managed components（若 `main/idf_component.yml` 有声明）会在 `set-target`/`reconfigure`/`build` 时自动拉取到 `managed_components/`。
- 未来若需 Tailscale / cloudflared：直接在 `.cursor/Dockerfile` 里安装（或运行时装），按官方 userspace 方式启动即可——环境本身就是容器，无需 docker-in-docker。

### 构建 / 运行 / 体积

标准命令（另见 `.github/workflows/build-esp-idf-project.yml`）：

- 构建：`idf.py build`
- 体积报告：`idf.py size` / `idf.py size-components` / `idf.py size-files`
- 无硬件运行（模拟器）：基础镜像**实测已内置** `qemu-system-riscv32` 与 `qemu-system-xtensa`（在 `/opt/esp/tools/qemu-*`），但 **ESP32-P4 的 `idf.py qemu` 官方尚未支持**（ESP-IDF 文档标注为 work in progress、未文档化），故本环境不为 P4 做 QEMU 验证，Cloud 内只做 `idf.py build` 构建验证。如确需无硬件仿真，可另行使用社区/官方的 `espressif/esp-emulator`（Rust 版 RISC-V 模拟器，支持 ESP32-P4；用法示例 `idf.py set-target esp32p4 && idf.py build && idf.py merge-bin` 后 `esp-emu --chip esp32p4 --firmware build/merged_flash.bin --net user`），但它不属于本环境的默认组成。

### 选择运行哪个 demo

实际生效的 `app_main` 由 `main/CMakeLists.txt` 中 `SRCS` 列表里未被注释的那个源文件决定；其余源文件以注释形式并列在同一处作为备选。切换 demo 只需在该 `SRCS` 中注释 / 取消注释对应文件并重新构建。

### 测试 / lint

本仓库没有单元测试或 lint 配置。CI 仅构建固件，并输出 `idf.py size` 与 `esptool image-info`（CI 已兼容 esptool 4.x/5.x 两种子命令）。
```

- [x] **Step 2：校验锚点标题存在**

Run: `grep -n 'Cursor Cloud specific instructions' AGENTS.md && test "$(grep -cE 'esp32s3|ESP32-S3' AGENTS.md)" -eq 0 && echo OK`
Expected: 命中锚点标题，末尾打印 `OK`（用 `test -eq 0` 确保「无残留」时退出码为成功；只查 ESP-Pocket2 特有的 ESP32-S3 残留。`qemu-system-xtensa` **不再**纳入——AGENTS 现按容器实测正面说明「镜像已内置 qemu-system-riscv32/xtensa」；`submodule`/`libslirp0` 因正文有正确表述也不纳入）。

---

### Task 5：分逻辑提交并推送

**Files:** 无新增，仅 `git` 操作。

> commit message 规范：标题用 cz 结构 `type(scope): emoji subject`；正文（body）用 `-` 列表并列多个要点、解释 what/why（每个逻辑单元单行、不硬折行）；footer `Co-authored-by:` 由平台 `commit-msg` hook 自动注入，无需手写。以下各提交用 `git commit -m 标题 -m 正文`（每个 `-m` 一段，标题与正文分离）。
> 对「已推送、需补 body」的既有提交：因平台设置了 `GIT_SEQUENCE_EDITOR`（导致 `git rebase -i` 行为不可控），改用 `git reset --soft origin/dev` 解散提交后按下方 Step 2–6 逐个重建，再 `git push --force-with-lease`（本分支仅本人使用、PR 为 draft 未 review，安全）。

- [x] **Step 1：确认处于 Task 0 创建的分支**

Run: `git branch --show-current`
Expected: `cursor/cloud-agent-env-5858`（分支已在 Task 0 基于最新 `dev` 建好，此处不重复建分支）。

- [x] **Step 2：提交 1 —— sdkconfig.defaults**

```bash
git add sdkconfig.defaults
git commit -m "chore(sdkconfig.defaults): 🔧 固定 esp32p4 构建目标并适配 revision v1.3 芯片" -m "- CONFIG_IDF_TARGET 设为 esp32p4: 固定构建目标, 全新环境(无 sdkconfig)执行 idf.py build 开箱即用、不回退默认 esp32, 并与 CI(ESP_IDF_TARGET: ESP32P4)一致
- CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y: 适配 revision 低于 v3.0 的工程样片(实测板卡 v1.3), 否则 esptool 以 requires chip revision in range [v3.0 - v3.99] 报错拒绝烧录
- CONFIG_ESP32P4_REV_MIN_0=y: 最低支持修订 Rev v0.0, 向下兼容 0.x/1.x(含 v1.3)
- 上述修订项仅影响真实烧录时的芯片校验, 对 Cloud 内纯构建无副作用; rev 低于 v3.0 与 v3.0 及以上互斥, 改用量产芯片需切换配置"
```

- [x] **Step 3：提交 2 —— .cursor 环境定义**

```bash
git add .cursor/Dockerfile .cursor/environment.json
git commit -m "docker(cloud-env): 🐳 新增 Dockerfile 化的 Cloud Agent 环境（ESP-IDF v5.5.4）" -m "- .cursor/Dockerfile 基于官方镜像 espressif/idf:v5.5.4@sha256:b9f2d6ea1c19e0c9f7959bdb74a9e3c775642f9d0f3b841937c5fa3363db892b(tag+digest 双锁定, tag 便于人读/digest 字节级不可变, 内置 ESP-IDF 与 riscv32-esp-elf 工具链, IDF_PATH=/opt/esp/idf)
- 仅补装基础镜像实测缺失项: sudo(Cursor 要求 git+sudo, 镜像含 git 缺 sudo)、开发 CLI(htop/vim/file/lsof/jq/net-tools/iproute2/dnsutils)、dfu-util
- 不重复安装镜像已自带项: git/less/ccache/git-lfs/libslirp0/qemu-system-riscv32+xtensa(2026-07-05 容器实测)
- 在 /etc/bash.bashrc 自动 source export.sh, 交互式 shell 可直接使用 idf.py
- 不 COPY 源码, 由 Cursor 自行 checkout 正确 commit
- .cursor/environment.json 采用 Dockerfile 模式(build.dockerfile=Dockerfile, context=..), install 执行 python3 fetch_repos.py --yes(依赖走 fetch_repos.py 而非 submodule, 幂等/非交互, repos.json 为空则跳过)
- 仓库 .cursor 配置优先级高于个人/团队快照, 无需 dashboard 建环境或删快照"
```

- [x] **Step 4：提交 3 —— AGENTS.md**

```bash
git add AGENTS.md
git commit -m "docs(agents): 📝 新增 Cloud Agent 环境与构建说明" -m "- 说明 Cloud Agent 环境(Dockerfile 模式): ESP-IDF 路径 /opt/esp/idf、解析优先级、export.sh 自动 source
- 记录构建目标 esp32p4 与 revision 配置由 sdkconfig.defaults 声明式固定, 生成的 sdkconfig 不提交
- 依赖机制: dependencies/ 目录由 fetch_repos.py(读 repos.json)管理, 而非 git submodule
- 构建/体积命令、demo 切换方式(main/CMakeLists.txt 的 SRCS)
- 说明 ESP32-P4 的 idf.py qemu 官方尚未支持, Cloud 内仅做 idf.py build 验证, 无硬件仿真可选 esp-emulator
- 约定与本仓库交互始终使用中文回复"
```

- [x] **Step 5：确认无生成物误加（此时先不提交 superpowers 文档）**

Run: `git status --porcelain && git log --oneline -3`
Expected: 三个新 commit（sdkconfig / .cursor / AGENTS.md）就位；工作区剩 `docs/superpowers/...`（untracked，**本步暂不提交**）与被忽略项；确认无 `build/`、生成物 `sdkconfig`、`dependencies/` 被加入。

- [x] **Step 6：最后提交 superpowers 文档（spec + plan），作为本 PR 的最后一个 commit**

> 时机：**必须在 spec/plan 彻底更新完成、不再改动之后**执行——superpowers 文档是本 PR 的**最后一个提交**（用户 2026-07-05 明确要求提交并置于末尾）。执行本步前应确保 Task 1–4 全部落地、且本会话对 plan/spec 的所有修订都已写入文件。**注（draft 阶段仍可续修订）**：作为最后提交后，若仍需修订 spec/plan（如实测收尾、digest pin、复审整改），用 `git reset --soft` / `git commit --amend` 重建第 4 提交并 `force-with-lease` 推送即可；§14 修订记录会追加「提交之后」的后续轮次（如第十五、十六次）。

```bash
git add docs/superpowers/specs/2026-07-05-cloud-agent-env-design.md docs/superpowers/plans/2026-07-05-cloud-agent-env.md
git commit -m "docs(superpowers): 📝 新增 cloud-agent 环境的 spec 与 plan" -m "- specs/2026-07-05-cloud-agent-env-design.md: 设计文档(是什么/为什么)——需求、设计决策 D1-D5、架构与控制流、测试策略、会话 QA、调研依据、GPT 交叉评审历程与修订记录
- plans/2026-07-05-cloud-agent-env.md: 实现计划(怎么做)——Task 0-7 逐步实现与校验命令、Self-Review、执行方式建议
- 参照 yuangezhizao/ESP-Pocket2 PR #1 的 superpowers 规范组织文档
- 作为本 PR 最后一个提交合入 dev(应用户要求置于末尾)"
```

- [x] **Step 7：推送**

```bash
git push -u origin cursor/cloud-agent-env-5858
```
Expected: 四个 commit 依次为 `sdkconfig.defaults` → `.cursor` → `AGENTS.md` → `superpowers 文档`。

> 提交格式说明：agent 在非交互环境用 `git commit -m` 手写，与 `git cz`（cz-conventional-emoji@1.1.8）生成的 `typeName(scope): emoji subject` 结构对齐；type/emoji 取自其 `GITMOJI_TYPE_MAP`（chore→🔧、docs→📝、docker→🐳）。

---

### Task 6：创建 Draft PR

**Files:** 无。使用 PR 管理工具（非 `gh` 写操作）。

- [x] **Step 0：检查 PR 模板**

Run: `ls .github/PULL_REQUEST_TEMPLATE* .github/pull_request_template* .github/PULL_REQUEST_TEMPLATE/* PULL_REQUEST_TEMPLATE* 2>/dev/null || echo '(无 PR 模板)'`
Expected: 若存在 PR 模板，按模板组织下方 body；当前仓库预期无模板，则用下方「PR 正文草稿」。

- [x] **Step 1：创建 PR**
  - branch_name: `cursor/cloud-agent-env-5858`
  - base_branch: `dev`
  - draft: true
  - title: `docker(cloud-env): 🐳 Dockerfile 化 Cloud Agent 环境（ESP-IDF v5.5.4，tag+digest 双锁定）并固定 esp32p4 构建目标`
  - body: 见下方「PR 正文草稿」。

- [x] **Step 2：加标签 / 指派**
  - label: 始终为 `enhancement`（该 label 仓库已存在）——**用 Step 1 `ManagePullRequest` 创建 PR 返回的 `pr_url` 调用 `EditPullRequestLabels` 添加**（该工具需 `pr_url`，故必须在 Step 1 创建 PR 之后执行）。
  - assignee: `yuangezhizao`——**注意：当前 PR 工具（`ManagePullRequest` / `EditPullRequestLabels`）不支持设置 assignee，`gh` 为只读**，故 assignee 需**由用户在 GitHub UI 手动设置**（除非后续有可用工具）。
  - 仓库名大小写（实测 quirk）：git remote 为全小写 `wt9932p4-tiny`，而文档/PR 链接用规范大写 `WT9932P4-TINY`。`ManagePullRequest`（create_pr/update_pr）与 `EditPullRequestLabels` 对 `pr_url` 做**大小写敏感**比较，传大写会报 `PR URL must belong to the current repository`（GitHub 本身与 `gh` 大小写不敏感、不受影响）。**绕过：把 `pr_url` 的仓库 slug 用与 remote 一致的小写 `wt9932p4-tiny` 重试即成功**（本会话与 Task 7 新会话均实测复现）。此为 Cursor 工具链 quirk，不影响 Dockerfile 环境本身。

- [x] **Step 3：确认 CI**
  - PR 触发 `Build ESP-IDF project`（`dev` 的 PR）。matrix：`v5.5.4`（必过）+ `latest`（experimental，允许失败）。
  - 说明：CI 的 `esp-idf-ci-action` 显式传 `target: ESP32P4`（见 workflow），因此 CI 证明的是「项目在 esp32p4/v5.5.4 下可构建」，**不**证明「不手动 set-target 也能靠 `sdkconfig.defaults` 自动选中 esp32p4」——后者（开箱即用）与 Dockerfile 生效由 Task 7 验证。产物 `build/WT9932P4-TINY.bin`。

#### PR 正文草稿（Task 6 建 PR 时初稿 · 历史留档）

> 注：这是 Task 6 **创建 PR 时**的初始正文；其后 PR 正文经 Task 7 实测回填与第十六次 digest pin 多次 `update_pr` 更新（新增实测证据表、「原 tag-only」与「digest pin 后未覆盖」说明等）。**当前实际正文以 [PR #2](https://github.com/yuangezhizao/WT9932P4-TINY/pull/2) 为准**，此处初稿不再逐次同步。

```markdown
## 概述

参考 ESP-Pocket2#1，把 WT9932P4-TINY 的 Cloud Agent 开发环境「配置即代码」（Dockerfile 模式），不再依赖个人快照；并让全新环境 `idf.py build` 对 ESP32-P4（含 revision v1.3 芯片）开箱即用。

> 设计文档：[spec](https://github.com/yuangezhizao/WT9932P4-TINY/blob/dev/docs/superpowers/specs/2026-07-05-cloud-agent-env-design.md) ｜ [plan](https://github.com/yuangezhizao/WT9932P4-TINY/blob/dev/docs/superpowers/plans/2026-07-05-cloud-agent-env.md)（随本 PR 最后一个提交合入 `dev`）；参考实现：[ESP-Pocket2 #1](https://github.com/yuangezhizao/ESP-Pocket2/pull/1)。

## 核心改动（4 个源码/配置文件；另含 superpowers spec/plan 文档，随最后一个提交合入）

- **`.cursor/environment.json`**（Dockerfile 模式）：`build.dockerfile=Dockerfile`、`build.context=..`；`install` 为 `python3 fetch_repos.py --yes`（本项目用 fetch_repos.py 管理 `dependencies/`，而非 git submodule；幂等、非交互；当前 `repos.json` 为空则跳过）。
- **`.cursor/Dockerfile`**：`FROM espressif/idf:v5.5.4@sha256:b9f2d6ea…`（tag+digest 双锁定；内置 ESP-IDF + riscv32-esp-elf）；仅补装**实测缺失项**：`sudo`、开发 CLI（htop/vim/file/lsof/jq/net-tools/iproute2/dnsutils）、dfu-util；在 `/etc/bash.bashrc` 自动 `source export.sh`。实测镜像**已自带** git/less/ccache/git-lfs/`libslirp0`/`qemu-system-riscv32`+`xtensa`，故不重复安装；ESP32-P4 的 `idf.py qemu` 官方未支持，本环境不做 QEMU 验证。
- **`sdkconfig.defaults`**：`CONFIG_IDF_TARGET="esp32p4"` 固定构建目标；`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_0=y` 适配 revision v1.3 芯片（否则 esptool 拒绝烧录）。注意 rev <3.0 与 ≥3.0 互斥——本配置面向 <3.0 样片，换量产 ≥3.0 芯片需切换。
- **`AGENTS.md`**：Cloud Agent 环境（Dockerfile 模式）说明——ESP-IDF 路径 `/opt/esp/idf`、依赖走 `fetch_repos.py`、demo 由 `main/CMakeLists.txt` 的 `SRCS` 决定；并说明 **ESP32-P4 的 `idf.py qemu` 官方尚未支持**，Cloud 内仅 `idf.py build` 验证（无硬件仿真可选 `esp-emulator`）。

## 关键设计点

- **无需删除个人快照、无需 web 端创建环境**：仓库 `.cursor/environment.json` 优先级最高，自动覆盖个人快照（后者仅 fallback）。
- **可复现（tag+digest 双锁定）**：镜像 `FROM` pin 到 `v5.5.4@sha256:b9f2d6ea…`（tag 便于人读、digest 保证字节级不可变）；构建目标由 `sdkconfig.defaults` 固定。
- **与 ESP-Pocket2 的差异**：芯片 esp32p4(RISC-V) vs esp32s3(Xtensa)；依赖 fetch_repos.py vs submodule；实测镜像已自带 libslirp0/qemu（ESP-Pocket2 额外装 libslirp0 实为冗余），本 PR 不重复装；P4 无 `idf.py qemu`，AGENTS.md 改写 QEMU 章节。

## 验证

- CI（`esp-idf-ci-action` 传 `target: ESP32P4`）证明「项目在 esp32p4/v5.5.4 下可构建」；但因 CI 显式指定了 target，**不**能证明「开箱即用（无需 set-target）」与 Dockerfile 生效。
- 「开箱即用 + Dockerfile 环境 + revision v1.3 烧录范围」由**新开 Cloud Agent**（Task 7）验证（当前会话为旧个人快照，不重建环境）。
```

---

### Task 7（验证，需新会话）：新开 Cloud Agent 验证「开箱即用」

> 当前会话（旧个人快照）无法验证 Dockerfile 生效，本任务由用户新开一个「从 `cursor/cloud-agent-env-5858` 分支启动」的 Cloud Agent 执行。
>
> ✅ **已完成（2026-07-05）**：新开 Cloud Agent 实测通过——全部必过项 + 诊断项均通过，结果已回填至 PR 正文「验证」表、并留档于 spec 第 8 节「实测结果」（完整命令 + 证据表）。要点：环境确由 `.cursor/Dockerfile` 构建（无回退）、`idf.py --version` 与 `git describe` 均 v5.5.4、未手动 `set-target` 即 `idf.py build` 成功（日志 `guessed 'esp32p4' from sdkconfig`）、产物 `build/WT9932P4-TINY.bin`、三条 `sdkconfig` 精确匹配（派生 `ESP_REV_MIN_FULL=0`/`MAX_FULL=199`）、bootloader/app 芯片修订范围 `v0.0–v1.99`（低于 v3.0，适配 v1.3 样片）；诊断项 9 顺带确认 `bash -lc` 也能用 `idf.py`（非交互 login shell 亦可用），故无需在 Dockerfile 补 `/etc/profile.d/*.sh` 或 `ENV PATH`。（注：本次实测为 **digest pin 之前** 的快照；第十六次给 `FROM` 补 digest 后，其可拉取性 / 开箱构建由新 Cloud Agent 或专设 CI 另行验证。）

#### 新开 Cloud Agent 的提示词（Task 7 历史提示词 · digest pin 前 · 含验收标准）

> 注：以下为 Task 7（**digest pin 之前**）实际使用的历史提示词；若复用于 digest pin 后验证，请同时确认 `.cursor/Dockerfile` 的 `FROM` 含 `@sha256:b9f2d6ea…`。

```text
你在一个「从 cursor/cloud-agent-env-5858 分支启动」的全新 Cursor Cloud Agent 中工作。本仓库是 WT9932P4-TINY（面向 ESP32-P4 / RISC-V 的 ESP-IDF v5.5.4 固件项目）。本分支新增了 .cursor/environment.json + .cursor/Dockerfile，把 Cloud Agent 环境 Docker 化（基于官方镜像 espressif/idf:v5.5.4）。

任务：验证该 Dockerfile 化环境是否「开箱即用」，并把实测结果回填到本分支对应的 PR。

请依次执行，并逐条粘贴命令输出作为证据：
1. 确认环境由 .cursor/Dockerfile 构建（Environment ready，无「回退到基础镜像」warning）。
2. git -C /opt/esp/idf describe --tags
3. idf.py --version
4. python3 fetch_repos.py --yes        # install 命令；验证幂等、repos.json 为空则跳过
5. idf.py build                        # 不要手动 set-target（核心验收）
6. idf.py size
7. # 精确校验三条核心配置（连值一起校验、不止键名；命中注释不算）：
   grep -qx 'CONFIG_IDF_TARGET="esp32p4"' sdkconfig && \
   grep -qx 'CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y' sdkconfig && \
   grep -qx 'CONFIG_ESP32P4_REV_MIN_0=y' sdkconfig && echo 'sdkconfig OK' || { echo 'sdkconfig FAIL'; exit 1; }
   grep -E 'CONFIG_ESP_REV_MIN_FULL|CONFIG_ESP_REV_MAX_FULL' sdkconfig || true   # 附带看派生的 min/max（信息，不阻断）
8. # esptool 4.x/5.x 子命令兼容（与 CI 一致），bootloader 与 app 两个产物都查；任一产物 esptool 失败即 exit 1：
   for BIN in build/bootloader/bootloader.bin build/WT9932P4-TINY.bin; do \
     echo "== $BIN =="; \
     if python -m esptool image-info --help >/dev/null 2>&1; then \
       python -m esptool image-info "$BIN" || exit 1; \
     else \
       python -m esptool image_info "$BIN" --version 2 || exit 1; \
     fi; \
   done   # `|| exit 1` 只保证「esptool 命令成功」；chip rev 是否 <3.0 需【目视】输出的 Minimum/Maximum chip rev（不应是 v3.0 - v3.99），rev 正确性已由第 7 步 SELECTS_REV_LESS_V3 精确校验保证
9. （诊断，非阻断）bash -lc 'command -v idf.py && idf.py --version'   # 探测非交互 login shell 是否也能找到 idf.py

验收标准：
必过（阻断）：
- [ ] 环境从 Dockerfile 构建，无回退警告
- [ ] git -C /opt/esp/idf describe --tags 输出 v5.5.4
- [ ] idf.py --version 输出 ESP-IDF v5.5.4
- [ ] 未手动 set-target，idf.py build 成功
- [ ] 生成 build/WT9932P4-TINY.bin
- [ ] sdkconfig 含 CONFIG_IDF_TARGET="esp32p4"、CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y、CONFIG_ESP32P4_REV_MIN_0=y
- [ ] （目视）bootloader 与 app（build/WT9932P4-TINY.bin）的 esptool image-info 输出的 chip revision 范围都落在 <3.0（不应是 [v3.0 - v3.99]）——esptool 命令本身成功由第 8 步 `|| exit 1` 保证；rev <3.0 的自动保证是第 7 步 `SELECTS_REV_LESS_V3` 精确校验
诊断（非阻断，仅用于发现改进点）：
- [ ] 第 9 步 bash -lc 能找到 idf.py

通过后：用 ManagePullRequest 的 update_pr（传 branch_name 或 pr_url）在 PR 正文「验证」处补一张实测结果表（命令 + 结果），标注「本分支 Cloud Agent 实测于 <日期>」。
若必过项失败：粘贴失败命令与完整报错，先判断是环境配置问题（.cursor/Dockerfile 或 environment.json）还是 IDF 构建问题，再对症修复并重跑，不要跳过失败项。
关于第 9 步（诊断）：本镜像仅在 /etc/bash.bashrc 写了 source export.sh（对交互式 bash 生效）。若非交互 login shell（bash -lc）找不到 idf.py，而第 3、5 步的 idf.py 正常，说明平台以交互式 shell 执行命令、方案已可用，无需处理；仅当你的执行链路确实走非交互 shell 且 idf.py 缺失时，才在 .cursor/Dockerfile 里补 /etc/profile.d/*.sh 或 ENV PATH 后重跑。
说明：ESP32-P4 的 idf.py qemu 官方尚未支持，本任务不做 QEMU / 仿真验证，只验证构建。
```

- [x] **Step 1：起 Cloud Agent 于本分支**，确认环境从 `.cursor/Dockerfile` 构建（Environment ready，无回退警告）。
- [x] **Step 2：核对镜像**（并诊断非交互 shell）

Run: `git -C /opt/esp/idf describe --tags && idf.py --version && (bash -lc 'command -v idf.py' || echo '(诊断: 非交互 login shell 未找到 idf.py, 非阻断)')`
Expected: `v5.5.4`；`ESP-IDF v5.5.4`。`bash -lc` 探测为**诊断项**（非阻断）——找不到不算失败，仅当执行链路确实走非交互 shell 时才按提示词补 PATH。

- [x] **Step 3：开箱构建 + revision 校验**（esptool 子命令做 4.x/5.x 兼容）

Run: `idf.py build && grep -qx 'CONFIG_IDF_TARGET="esp32p4"' sdkconfig && grep -qx 'CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y' sdkconfig && grep -qx 'CONFIG_ESP32P4_REV_MIN_0=y' sdkconfig && for BIN in build/bootloader/bootloader.bin build/WT9932P4-TINY.bin; do echo "== $BIN =="; if python -m esptool image-info --help >/dev/null 2>&1; then python -m esptool image-info "$BIN" || exit 1; else python -m esptool image_info "$BIN" --version 2 || exit 1; fi; done`
Expected: 无需 `set-target` 即自动选中 esp32p4，生成 `build/WT9932P4-TINY.bin`；`sdkconfig` 三条核心配置精确匹配（`grep -qx`）；两产物 esptool 命令均成功（任一失败即 `exit 1`）。chip revision 是否 <3.0 需【目视】image-info 输出的 Min/Max chip rev（不应是 [v3.0 - v3.99]）；rev 正确性由 `SELECTS_REV_LESS_V3` 精确校验自动保证。

- [x] **Step 4：回填**：用 `ManagePullRequest` 的 `update_pr`（`branch_name`/`pr_url`）将实测结果补进 PR 正文「验证」表；若发现 QEMU/emulator 可用性有更新，再更新 `AGENTS.md`（当前默认按「P4 无 idf.py qemu」）。

---

## 三、Self-Review（写完计划的自检）

1. **Spec 覆盖**：D1(sdkconfig.defaults+revision)→Task 1；D2(不装 git cz)→已在 Dockerfile 不含；D3(QEMU：不重复装 libslirp0、不做 P4 QEMU 验收)→Task 2/4；D4(排查工具)→Task 2；D5(分支/PR)→Task 0（建分支）/Task 5/6；Q3(标题)→Task 6 标题。均有对应任务。
2. **Placeholder 扫描**：各文件均为完整内容，无 TBD/TODO；D1 的 revision 已定稿为「`SELECTS_REV_LESS_V3=y` + `REV_MIN_0=y`」（用户 2026-07-05 复核确认）。
3. **一致性**：镜像 `espressif/idf:v5.5.4@sha256:b9f2d6ea…`（tag+digest 双锁定）、目标 `esp32p4`、`install=python3 fetch_repos.py --yes`、路径 `/opt/esp/idf` 在 environment.json / Dockerfile / AGENTS.md / PR 正文中一致；commit 的 type/emoji 与 cz map 一致（chore→🔧、docker→🐳、docs→📝）。
4. **范围**：聚焦 Cloud Agent 环境，未触碰 `.devcontainer/`、CI、`.vscode/` 等无关文件。

---

## 四、执行方式与建议

superpowers 提供两种执行方式（选择依据见 spec 第 10 节 Q「执行方式」的摘要；下方为本 plan 的执行建议）：

- **① `executing-plans`**：当前会话按任务顺序（或按批）执行 + 人工 checkpoint 审查。开销低、上下文连贯、人可控。
- **② `subagent-driven-development`**：每任务派 fresh 子代理实现 + 两阶段审查（spec 合规 → 代码质量）。质量门禁强、主上下文干净，但子代理调用多、开销大。

**本任务建议：① `executing-plans` / 直接逐任务执行。** 理由：本任务是 4 个纯配置/文档文件（`json`/`dockerfile`/`ini`/`md`），无代码逻辑、无单元测试（TDD 无从谈起），且真正的「开箱即用」验证只能靠新开 Cloud Agent 跑 `idf.py build`（当前会话无法验证）；因此 ② 的双审查 + 多子代理对本场景性价比低。执行时在「提交前」「开 PR 前」两个点向用户确认。

> 模型约束（用户规则）：不在 Task 工具指定 `model` 时，子代理沿用发起该 Task 的当前会话模型，否则禁止创建；要用 GPT 等其他模型做 review 须显式指定 `model`（如 `gpt-5.5-high`）。**Task 工具用法、可传入的 model slug 列表、以及本计划的 GPT 交叉评审历程与修订记录，均已归档在 spec 第 12、13、14 节。**
