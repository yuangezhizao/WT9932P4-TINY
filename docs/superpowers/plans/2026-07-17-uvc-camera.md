# 摄像头 USB UVC 例程 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: 用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现本计划。步骤用复选框（`- [ ]`）语法跟踪。

**Goal:** 给 WT9932P4-TINY（ESP32-P4）新增 `main_uvc.c` 摄像头 demo 并设为默认激活，实现「OV5647(RAW10 1080p30) → P4 ISP → 硬件 JPEG(MJPEG) → USB UVC(Device)」链路，让电脑把开发板当标准 USB 摄像头，`idf.py build` 开箱即构建本例程。

**Architecture:** dev 采用「仓库根单一 `project()` + `main/CMakeLists.txt` 的 `SRCS` 注释切换 demo」脚手架（方案 A）。本例程以官方 `uvc_example.c` 为应用层蓝本，并在 `main/main_uvc.c` 完成本板适配：GPIO0 拉高使能 OV5647 电源、5 秒周期 monitor 输出 fps/CPU/内存/逐任务信息；UVC 最终默认 **bulk** 输出。**双蓝本三层分工**：UVC 应用层参照乐鑫 `esp_video/examples/uvc`；板级层完整复制官方 `example_video_common` 到本地 `components/`（customized 板、SCCB 8/7、`XCLK_PIN=-1`、CSI 2.5V LDO），以同板 `video_lcd_display` 为板级权威；传感器层 OV5647 自配（RAW10 1080p30、模块自带 25MHz 晶振）。依赖经 Registry `^` 范围引入、不提交 `dependencies.lock`。

**Tech Stack:** ESP-IDF v5.5.4、ESP32-P4（RISC-V）、`esp_video ^2.3.0` / `usb_device_uvc ^1.3.1` / `esp_cam_sensor ^2.3.0`（Registry）、`example_video_common`（vendored 复制自 esp-video-components@`002e2c0`，与 `esp_video 2.3.0` 同源同版；其自身 manifest 无 version 字段）、OV5647 MIPI-CSI 2-lane、P4 ISP + 硬件 JPEG(M2M) 编码、USB UVC Device（HUSB / USB-HS 480Mbps）、V4L2、CMake、Kconfig。

**关联 Spec:** [`docs/superpowers/specs/2026-07-17-uvc-camera-design.md`](https://github.com/yuangezhizao/WT9932P4-TINY/blob/cursor/uvc-camera-example-03dd/docs/superpowers/specs/2026-07-17-uvc-camera-design.md)（主体已实现并真机点亮，含 25MHz 兼容性、isoc→bulk 调优与 Task 13 可选停流看门狗设计）

**关联 PR:** [#4](https://github.com/yuangezhizao/WT9932P4-TINY/pull/4)（draft，分支 `cursor/uvc-camera-example-03dd` → `dev`）

---

## 执行前必读

- **验证方式 = 构建**：本仓库无单元测试 / lint 框架（见 `AGENTS.md`「测试 / lint」与 spec §8）。因此每个任务的"验证"用 `idf.py build`（及必要时 `idf.py size`、`grep sdkconfig` 配置断言）代替单元测试，**不要自行发明单元测试**（超出本 plan 范围）。这是本仓库对 writing-plans「TDD」的既定适配（blink plan 亦然）。
- **Cloud 只能 build 验证**：UVC 出图、帧率、macOS 查看、OV5647 探测/25MHz 时序、同向 FPC 接线等**运行时行为无法在 Cloud 验证**（见 spec §7/§8），需用户本地烧录测试。本 plan 的 Cloud 目标 = 构建通过 + 配置断言 + 体积检查；运行时项以「留给硬件验证」标注、不阻塞 plan 完成。
- **工具约定**：下文出现的 `cp`/`grep`/`cat`/`sed`/`find` 等只是**面向人类的等价示例**；agentic worker 请用编辑器的 `Read`/`Grep`/`StrReplace`/`Write` 等工具完成同等查看/编辑（`Read`/`Grep` 受限于 workspace，读 `/tmp` clone 时可用 shell）。
- **依赖污染非"零副作用"（方案 A 固有，双向）**：一旦 `main/idf_component.yml` 声明 `esp_video` 等，**即使 `SRCS` 仍是 blink**，component manager 也会拉取并编译 `esp_video`/`usb_device_uvc`/`esp_cam_sensor` 及其传递依赖（见 spec §7）。故 Task 1/Task 2 的 blink 构建即「首次验证依赖解析 + 组件编译」，不要当成理所当然绿色。
- **构建需可达网络（两个来源）**：① ESP Component Registry（`components.espressif.com`）拉取 `esp_video`/`usb_device_uvc`/`esp_cam_sensor`（拉取后缓存于 `managed_components/`）；② GitHub（`github.com`）clone esp-video-components 以复制 `example_video_common` 与 `uvc_example.c`（Task 2/Task 6）。egress 受限环境会导致相应步骤失败 → 按「偏差处理」汇报。
- **`sdkconfig.defaults` 屏蔽陷阱（重要）**：`sdkconfig.defaults*` 只对「`sdkconfig` 中尚不存在的符号」生效（先加载 defaults、再加载既有 `sdkconfig`，后者压过前者），且 `idf.py fullclean` 只清 `build/`、**不删 `sdkconfig`**。故每次改完 `sdkconfig.defaults*` 后要 `rm -f sdkconfig` 再 `idf.py reconfigure`/`build`，否则新默认不生效。本 plan 在 Task 3 与主验证（Task 7）前均 `rm -f sdkconfig`。
- **版本锚点（可复现）**：esp-video-components 是 monorepo、**无 git tag**（靠 Registry 发版）。本 plan 把 clone 锁定到 commit `002e2c01b41d8819c603bd18a108296c8e925ea1`（其 `esp_video/idf_component.yml` = `version: "2.3.0"`）。该 commit 相对 2.3.0 发布点仅有 3 条 `esp_video` 组件内部改动（H.264/ISP 相关），**不涉及** `example_video_common` 与 `uvc_example.c`，故复制的两者与 Registry esp_video 2.3.0 匹配。
- **提交约定**：遵循 cz 格式（gitmoji + `type(scope)`；仓库未装 cz 工具，手写符合该格式即可）。**emoji 优先用 gitmoji 短代码形式**（新提交为主；既有历史不追改、见下方「emoji 实况」注）（如 `:sparkles:` / `:heavy_plus_sign:` / `:memo:`，而非 unicode 字符 :sparkles:/:heavy_plus_sign:/:memo:——短代码纯 ASCII、GitHub 渲染为彩色 emoji、不受终端/客户端字体影响）。正文用 `-` 紧凑列出要点（subject 与正文间空一行、正文各要点间不留空行），scope 用主文件名/主题。**注：emoji 实况**——C1–C5 与 C7(docs) 沿用 unicode（`➕📦✨🐛📊📝`）、C6(Task 13 停流看门狗) 用 gitmoji 短代码 `:sparkles:`（Task 13 起改用短代码、不追改既有历史，故最终 `git log` emoji 混排）。`Co-authored-by: 远哥制造 <yuangezhizao@users.noreply.github.com>` 由 cloud 平台 hook 自动补加（当前 7 提交均带此 trailer，无需手写）。本 plan 最初计划 5 个逻辑提交（C1–C5，C3 拟合并 Task 3–7）；真机调试 + Task 13 后**实际为 7 个物理提交**（与 PR / `git log` 一致、doc-last；hash 随 amend 变动、以 `git log` 为准）。物理提交如下（每个提交后代码可构建）：
  - **C1** `build(deps): ➕ 引入 esp_video/usb_device_uvc/esp_cam_sensor 依赖`（Task 1）
  - **C2** `feat(example_video_common): 📦 vendored 复制官方板级公共组件`（Task 2）
  - **C3** `feat(main_uvc): ✨ 新增 OV5647→ISP→MJPEG→USB UVC 例程并默认激活`（Task 3–7：main_uvc.c 主体 + sdkconfig 配置[1920×1080@30/bulk/JPEG80/ISP=n] + Kconfig；AGENTS 不在此提交、仅 C7 定稿）
  - **C4** `fix(main_uvc): 🐛 启动拉高 GPIO0 使能摄像头电源以修复 OV5647 detect 失败`（真机调试）
  - **C5** `feat(main_uvc): 📊 新增 monitor task 每 5 秒打印 fps/CPU/内存`
  - **C6** `feat(main_uvc): :sparkles: 可选停流功耗看门狗（EXAMPLE_UVC_IDLE_STREAMOFF，默认开）`（Task 13）
  - **C7** `docs(docs/superpowers): 📝 新增 uvc 例程 spec 与实现计划（含真机调试记录）`（Task 11 AGENTS + Task 12 spec/plan + Task 13 文档合并入此、未单列 C8；doc-last 殿后）
- **生成物勿入库**：`sdkconfig`、`managed_components/`、`dependencies/`、`build/` 均已被 `.gitignore` 忽略；`dependencies.lock` **未** gitignore（构建生成、为未跟踪文件，按 spec D9 不提交）。提交时用**精确 `git add <path>`，切勿 `git add -A`**（尤其防误加 `dependencies.lock`、`sdkconfig`、`components/example_video_common/` 之外的临时物；另注本会话有 `.ref_tmp/`、`.cursor/skills/grilling/` 两个已在 `.git/info/exclude` 忽略的本地目录，勿提交）。
- **偏差处理（用户规则）**：若某步实际结果与预期不符（尤其依赖解析、esp_video/example_video_common 编译、配置断言、体积超限），**STOP** 并以「发现偏差：XXX，是否允许加入 plan？」汇报，确认后再在 plan 中更新、再实现。不得擅自加入 plan 未包含的内容。
- **真机状态与剩余风险**：GPIO0 上电后 OV5647 探测/UVC 出图已验证；初始 isoc 受约 65Mbps 带宽限制，最终默认改为 bulk。剩余关注：① 25MHz 晶振相对驱动 24MHz 的 +4.17% 时序偏差对抗频闪/精确帧率的影响；② bulk 下设备端串行取帧/编码瓶颈及不同 host 兼容性；③ 反向 FPC 会使电源反接；④ Task 13 停流功耗回落 / PROBE→COMMIT 恢复已真机达成，suspend→resume / isoc 等边界待验证；in-flight `fb_get` 竞态（Major-1）为已知边界，根治方案 B（DQBUF 超时，参照 esp_capture）另开新 PR，机理/对照/方案见 spec Q42–Q44 与 §13 两图。

---

## File Structure

**新建：**
- `main/main_uvc.c` —— 基于官方 `uvc_example.c` 的 UVC 摄像头 demo，已增加 GPIO0 上电适配与 5 秒周期 monitor；采集经 MJPEG 编码后通过 UVC bulk 输出。初始导入时为 416 行逐字蓝本，后续真机调试已演进，不再与上游逐字一致。
- `components/example_video_common/` —— **完整复制**官方 `esp_video/examples/common_components/example_video_common/`（14 个文件，含 `example_init_video.c`/`example_encoder.c`/`example_storage.c`/`Kconfig.projbuild`/`CMakeLists.txt`/`idf_component.yml`/`README.md`/`include/example_video_common.h` + `include/boards/<6 块板>/example_video_common_board.h`）。`CMakeLists.txt` `REQUIRES esp_video fatfs`；选 customized 板时仅 `include/boards/customized/` 参与编译；其 `idf_component.yml` 连带拉 `esp_new_jpeg`（P4 走硬件 JPEG、冗余无害）。`example_storage.c`/`example_encoder.c` 恒编译但 uvc 不调用（冗余无害）。

**修改：**
- `main/idf_component.yml` —— 现有仅 `led_strip ^3.0.3`；追加 `esp_video ^2.3.0`、`usb_device_uvc ^1.3.1`、`esp_cam_sensor ^2.3.0`（后者为冗余的防御性显式声明，见 spec D9/Q22）。**不含** `example_video_common`（走本地 `components/`，IDF 默认扫描）。
- `sdkconfig.defaults` —— 追加摄像头板级/传感器/视频管线/UVC/RTOS/分区配置（按功能分段注释，见 Task 3，内容基于 spec §5 增量清单；SPIRAM 等模组内存硬件已归 `.esp32p4`）。
- `sdkconfig.defaults.esp32p4` —— 追加 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` 与 `SPIRAM`/`SPIRAM_SPEED_200M`（+ SPIRAM_MODE/EXPERIMENTAL 说明注）等 N16R32 模组硬件（P4 revision 两项不动）。
- `main/Kconfig.projbuild` —— **追加** `EXAMPLE_JPEG_COMPRESSION_QUALITY`（`if FORMAT_MJPEG_CAM1`，默认 80）、H.264 参数项（`if FORMAT_H264_CAM1`：`I_PERIOD`/`BITRATE`/`MIN_QP`/`MAX_QP`，对齐蓝本）与停流看门狗选项（`EXAMPLE_UVC_IDLE_STREAMOFF`/`_MS`，menu 顶层、与编码格式解耦），**保留** blink 的 `BLINK_*` 菜单（否则切回 blink 缺符号）。
- `main/CMakeLists.txt` —— `SRCS` 将 `main_uvc.c` 设为默认激活，其余（`main.c`/`main_hello_world.c`/`main_blink_example.c`）注释。
- `AGENTS.md` —— 「选择运行哪个 demo」章节纳入 uvc 例程条目、并把「默认激活」从 blink 移到 uvc。
- `docs/superpowers/specs/2026-07-17-uvc-camera-design.md` —— 文档提交（并按 PR #4「已知待办」#2/#3 统一轮次口径与日期，见 Task 12）。**注：本 File Structure 用逻辑编号；文档按 doc-last 约定随最后的物理提交 C7 入库（非逻辑 C5）**。
- `docs/superpowers/plans/2026-07-17-uvc-camera.md` —— 本文件，同随物理提交 C7（doc-last）入库。

**保持不变：**
- 其余 demo 源文件（`main/main.c`/`main_hello_world.c`/`main_blink_example.c`）、顶层 `CMakeLists.txt`（`EXTRA_COMPONENT_DIRS dependencies` 已存在；本地 `components/` 由 IDF 默认扫描，无需改）、CI（`.github/workflows/build-esp-idf-project.yml`）、`.devcontainer`/`.vscode`/`fetch_repos.py`/`repos.json`、`.cursor/`、P4 revision 两项。

**默认不新增（仅 fallback 才需）：**
- `partitions.csv` —— 默认用内置 `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`（factory ~1.5MB）；仅当 Task 8 的 `idf.py size` 显示 app ≥1.5MB 才 fallback 到 `CONFIG_PARTITION_TABLE_CUSTOM` + 新增 `partitions.csv`（见 spec D14，大概率不触发）。

---

## Task 1: `main/idf_component.yml` 追加三依赖并验证解析/编译

**Files:**
- Modify: `main/idf_component.yml`
- 生成但**不提交**：`dependencies.lock`（项目根，未跟踪）、`managed_components/`（已 gitignore）

- [x] **Step 1: 把 `main/idf_component.yml` 改为如下内容**

保留原有 `idf` 约束与 `led_strip`，追加三个 Registry 依赖（乐鑫官方组件省略 `espressif/` 前缀；用 `^` 范围、不锁精确版本，沿用仓库 blink 风格）：

```yaml
## IDF Component Manager Manifest File
dependencies:
  ## Required IDF version
  idf:
    version: ">=5.5.0"
  # blink demo 依赖
  led_strip: "^3.0.3"
  # uvc demo 依赖（Registry；OV5647 → ISP → 硬件 JPEG(MJPEG) → USB UVC Device）
  # esp_video 会对 P4 传递拉取 esp_cam_sensor/esp_ipa/esp_h264/cmake_utilities/usb_host_uvc；
  # esp_cam_sensor 为冗余的防御性显式声明（确保 CONFIG_CAMERA_OV5647 稳定可配，见 spec D9/Q22）。
  esp_video: "^2.3.0"
  usb_device_uvc: "^1.3.1"
  esp_cam_sensor: "^2.3.0"
```

- [x] **Step 2: 保持 blink 激活，干净重建以触发依赖解析/拉取 + 编译**

Run: `cd /workspace && rm -f sdkconfig && idf.py fullclean && idf.py build`
Expected: `Project build complete.`，产物 `build/WT9932P4-TINY.bin` 生成。首次会联网拉取依赖到 `managed_components/`（较慢）。此时 `SRCS` 仍是 blink，但 `esp_video`/`usb_device_uvc`/`esp_cam_sensor` 因依赖污染会被编译——本步即「首次验证依赖解析/拉取 + 组件编译」（`usb_device_uvc 1.3.x` 依赖 `tinyusb ^0.19` 的版本解析、`esp_video` 对 P4 的传递依赖，见 spec D9），从而保证 C1 提交后可构建（`example_video_common` 的编译在 Task 2 验证）。
（偏差预案：若依赖无法解析 / 版本冲突（尤其 tinyusb）/ registry 不可达，或 `esp_video`/`esp_cam_sensor` 因缺省未选传感器而编译报错，STOP 汇报——后者可能需把 Task 3 的传感器/板级配置提前与本 Task 合并。）

- [x] **Step 3: 断言依赖已拉取**

Run: `ls managed_components/`
Expected: 含 `espressif__esp_video`、`espressif__usb_device_uvc`、`espressif__esp_cam_sensor`，以及传递依赖（`espressif__esp_ipa`、`espressif__esp_h264`、`espressif__esp_sccb_intf`、`espressif__cmake_utilities`、`espressif__usb_host_uvc` 等；`esp_new_jpeg` 由 `example_video_common` 声明、Task 2 复制后才拉取，此处不应出现）。
（等价 shell：`ls managed_components | rg -i 'esp_video|usb_device_uvc|esp_cam_sensor'`）

- [x] **Step 4: 提交（C1）**

```bash
git -C /workspace add main/idf_component.yml
git -C /workspace commit -F - <<'EOF'
build(deps): :heavy_plus_sign: 引入 esp_video/usb_device_uvc/esp_cam_sensor 依赖

- main/idf_component.yml 追加 esp_video ^2.3.0、usb_device_uvc ^1.3.1、esp_cam_sensor ^2.3.0（Registry）
- 沿用仓库风格：^ 范围锁大版本、不提交 dependencies.lock
- esp_cam_sensor 为冗余的防御性显式声明（确保 CONFIG_CAMERA_OV5647 稳定可配）
- 为 uvc demo（OV5647→ISP→MJPEG→USB UVC Device）准备依赖
EOF
```

---

## Task 2: 复制 `example_video_common` 到本地 `components/`

**Files:**
- Create: `components/example_video_common/`（完整复制，14 个文件）

- [x] **Step 1: clone esp-video-components 并锁定版本 commit**

```bash
cd /tmp && rm -rf esp-video-components
git clone --filter=blob:none https://github.com/espressif/esp-video-components.git
cd esp-video-components && git checkout 002e2c01b41d8819c603bd18a108296c8e925ea1
```
Expected: clone 成功、`HEAD` 位于 `002e2c0`。校验版本：`grep -m1 version esp_video/idf_component.yml` → `version: "2.3.0"`。
（偏差预案：若该 commit 已不可达或 `version` 非 2.3.0，STOP 汇报。）

- [x] **Step 2: 完整复制到本地 `components/`**

```bash
mkdir -p /workspace/components
cp -r /tmp/esp-video-components/esp_video/examples/common_components/example_video_common /workspace/components/
```

- [x] **Step 3: 校验复制完整（14 个文件、customized 板头存在）**

Run: `find /workspace/components/example_video_common -type f | sort`
Expected（逐一存在）：
```
components/example_video_common/CMakeLists.txt
components/example_video_common/Kconfig.projbuild
components/example_video_common/README.md
components/example_video_common/example_encoder.c
components/example_video_common/example_init_video.c
components/example_video_common/example_storage.c
components/example_video_common/idf_component.yml
components/example_video_common/include/boards/customized/example_video_common_board.h
components/example_video_common/include/boards/esp32-p4-eye/example_video_common_board.h
components/example_video_common/include/boards/esp32-p4-function-ev-board-v1.4/example_video_common_board.h
components/example_video_common/include/boards/esp32-p4-function-ev-board-v1.5/example_video_common_board.h
components/example_video_common/include/boards/esp32-s3-eye/example_video_common_board.h
components/example_video_common/include/boards/esp32-s31-korvo/example_video_common_board.h
components/example_video_common/include/example_video_common.h
```
断言 `CMakeLists.txt` 含 `REQUIRES "esp_video" "fatfs"`、customized 分支 `list(APPEND inc_dirs "include/boards/customized")`。

- [x] **Step 4: 保持 blink 激活，构建验证 `example_video_common` 可编译**

Run: `cd /workspace && rm -f sdkconfig && idf.py fullclean && idf.py build`
Expected: `Project build complete.`。此时 `components/example_video_common`（IDF 默认扫描）被 main 自动 require 而编译（其 `REQUIRES esp_video` 由 Task 1 已满足）；连带拉取 `esp_new_jpeg`。板级 include 目录此时用 Kconfig 默认的 Function-EV-Board V1.5（尚未在 Task 3 选 customized），能编译。
（预期告警：本仓库 `main/Kconfig.projbuild` 与复制来的 `example_video_common/Kconfig.projbuild` 都 `orsource` 同一 `env_caps`，故可能出现 `ENV_GPIO_*` 重复定义的 kconfig 告警——属预期、不阻塞构建；仅当它升级为 error 才需处理。）
（偏差预案：若 `example_video_common` 编译失败/找不到 `esp_video` 头，STOP 汇报。）

- [x] **Step 5: 提交（C2，精确 add 整个目录、勿 `-A`）**

```bash
git -C /workspace add components/example_video_common
git -C /workspace commit -F - <<'EOF'
feat(example_video_common): :package: vendored 复制官方板级公共组件

- 完整复制 esp-video-components@002e2c0 的 example_video_common 到本地 components/
- 该组件非 Registry 组件（官方以 override_path 本地引用），外部工程须复制到本地
- REQUIRES esp_video+fatfs；选 customized 板时仅 include/boards/customized 参与编译
- example_storage.c/example_encoder.c/esp_new_jpeg 为冗余依赖、无害（P4 走硬件 JPEG）
EOF
```

---

## Task 3: 追加根 `sdkconfig.defaults` 摄像头/板级/UVC 配置

**Files:**
- Modify: `sdkconfig.defaults`（在文件末尾追加，保留现有 target/blink 两段）

- [x] **Step 1: 在 `sdkconfig.defaults` 末尾追加以下完整增量（基于 spec §5 增量清单，按功能分段注释；个别注释含实现期澄清）**

```ini

# =========================================================================
# uvc demo 配置（OV5647 → ISP → 硬件 JPEG(MJPEG) → USB UVC Device）
# 说明：以下为「追加到根 sdkconfig.defaults 的完整增量」；flash size 见 sdkconfig.defaults.esp32p4。
# =========================================================================

# 板级：Customized 开发板 + 启用 MIPI-CSI sensor + 本板引脚/时钟
# （必须显式设：example_video_common 选板默认是 Function-EV-Board V1.5，且 XCLK_PIN 仅 customized 板可配；否则 rm sdkconfig 后开箱构建会用错板）
CONFIG_EXAMPLE_SELECT_CUSTOMIZED_DEV_BOARD=y
CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_SENSOR=y
CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN=8
CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN=7
CONFIG_EXAMPLE_MIPI_CSI_XCLK_PIN=-1

# 传感器 OV5647（SCCB 自动探测）+ 采集/UVC 输出分辨率
# 关键：采集档与 UVC 尺寸必须成对一致（main_uvc.c 的 video_start_cb 中采集/编码/UVC 三处共用同一 width×height、不缩放）
# RAW10=10bit 高画质（1080p/1280×960）；RAW8=8bit 高帧率降采样（800×* 系列）
CONFIG_CAMERA_OV5647=y

# ===== 分辨率档：5 选 1（保留一组取消注释、其余整组注释；切换后须 rm sdkconfig && idf.py build）=====
# 档位按分辨率（像素面积）从高到低排列；默认取消注释最高档 [A]，其余整组注释
# ---[A] 1920×1080@30 RAW10（P4/OV5647 最高分辨率，默认；帧率最低）---
CONFIG_CAMERA_OV5647_MIPI_RAW8_800X640_50FPS=n
CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=n
CONFIG_CAMERA_OV5647_MIPI_RAW8_800X1280_50FPS=n
CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=n
CONFIG_CAMERA_OV5647_MIPI_RAW10_1920X1080_30FPS=y
CONFIG_CAMERA_OV5647_MIPI_DEFAULT_FMT_RAW10_1920X1080_30FPS=y
CONFIG_UVC_CAM1_FRAMESIZE_WIDTH=1920
CONFIG_UVC_CAM1_FRAMESIZE_HEIGT=1080
CONFIG_UVC_CAM1_FRAMERATE=30
# ---[B] 1280×960@45 RAW10（画质/帧率均衡，binning）---
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X640_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X1280_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1920X1080_30FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=y
# CONFIG_CAMERA_OV5647_MIPI_DEFAULT_FMT_RAW10_1280X960_BINNING_45FPS=y
# CONFIG_UVC_CAM1_FRAMESIZE_WIDTH=1280
# CONFIG_UVC_CAM1_FRAMESIZE_HEIGT=960
# CONFIG_UVC_CAM1_FRAMERATE=45
# ---[C] 800×1280@50 RAW8（竖屏）---
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X640_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1920X1080_30FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X1280_50FPS=y
# CONFIG_CAMERA_OV5647_MIPI_DEFAULT_FMT_RAW8_800X1280_50FPS=y
# CONFIG_UVC_CAM1_FRAMESIZE_WIDTH=800
# CONFIG_UVC_CAM1_FRAMESIZE_HEIGT=1280
# CONFIG_UVC_CAM1_FRAMERATE=50
# ---[D] 800×800@50 RAW8（帧率优先，1:1 方形）---
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X640_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X1280_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1920X1080_30FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=y
# CONFIG_CAMERA_OV5647_MIPI_DEFAULT_FMT_RAW8_800X800_50FPS=y
# CONFIG_UVC_CAM1_FRAMESIZE_WIDTH=800
# CONFIG_UVC_CAM1_FRAMESIZE_HEIGT=800
# CONFIG_UVC_CAM1_FRAMERATE=50
# ---[E] 800×640@50 RAW8（WVGA，5:4）---
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X1280_50FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW10_1920X1080_30FPS=n
# CONFIG_CAMERA_OV5647_MIPI_RAW8_800X640_50FPS=y
# CONFIG_CAMERA_OV5647_MIPI_DEFAULT_FMT_RAW8_800X640_50FPS=y
# CONFIG_UVC_CAM1_FRAMESIZE_WIDTH=800
# CONFIG_UVC_CAM1_FRAMESIZE_HEIGT=640
# CONFIG_UVC_CAM1_FRAMERATE=50
# ===== 分辨率档结束 =====

# 视频管线：RAW→RGB/YUV 由默认开启的 ISP video device 完成（编码器 device 见下方「编码格式档」，随 MJPEG/H264 各自开启、互斥）
# 注：RAW→RGB 由 ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE（自身 default y）完成，无需显式开；它与 CSI device 所 select 的内部符号 ESP_VIDEO_ENABLE_ISP 是两个不同符号（后者是内部开关、非设备节点）。
# ISP IPA 自动调优（AE/AWB/AF）：默认关闭——对齐官方 uvc 蓝本，缓解画面偏黄（开启时 IPA 的 AWB 疑似算偏致整体偏黄）。
# 若需自动曝光/白平衡/对焦，改 =y；进一步可自定义 OV5647 IPA JSON（CAMERA_OV5647_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE）。
CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=n

# ===== 编码格式档：2 选 1（保留一组取消注释、其余整组注释；切换后须 rm sdkconfig && idf.py build）=====
# main/Kconfig.projbuild 已备齐 MJPEG/H264 两套编码参数（对齐官方蓝本）；分辨率/帧率见上方「分辨率档」（两格式共用 WIDTH/HEIGHT/FRAMERATE）
# ---[MJPEG]（默认；UVC 通告 MJPEG、走硬件 JPEG 编码 device；JPEG 质量见下方 EXAMPLE_JPEG_COMPRESSION_QUALITY）---
CONFIG_FORMAT_MJPEG_CAM1=y
CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_ENC_VIDEO_DEVICE=y
# ---[H264]（走硬件 H264 编码 device；H264 码率/QP/I 帧周期见 menuconfig「Example Configuration」、有默认值）---
# CONFIG_FORMAT_H264_CAM1=y
# CONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE=y
# ===== 编码格式档结束 =====
# 注：编码器 device 随格式各自开启、互斥——MJPEG 只编 JPEG 编码器 device、H264 只编 H264 device（切换时整组注释即可、各省对方 device 体积）；MJPEG/H264 双态 Cloud 构建均验证通过

# FRAMESIZE choice 占位（该 choice 必须选一项；实际尺寸由上方分辨率档的 WIDTH/HEIGHT 显式覆盖）
CONFIG_FRAMESIZE_FHD=y
# 只通告一档（MULTI 默认 y 会额外通告 VGA/HVGA、与采集档不一致）
CONFIG_UVC_CAM1_MULTI_FRAMESIZE=n
# 禁用 USB MSC 存储（UVC 已占用 USB，防冲突；对齐官方 uvc 例程）
CONFIG_EXAMPLE_DISABLE_USB_MSC_STORAGE=y
# UVC 传输模式：默认改用 bulk（连续传输、带宽利用率高，1280×960 可上更高帧率；改回 isoc 注释掉下一行即可）。
# isoc vs bulk：isoc 保证固定带宽/无重传/每微帧仅 1 包(本库 1023B)上限低；bulk 无带宽保证但可占满总线/有重传/更适合大帧。
CONFIG_UVC_MODE_BULK_CAM1=y
# JPEG 压缩质量：显式声明为默认 80（范围 1-100；仅影响清晰度、不影响偏黄）；如需提帧率可降到 60 等
CONFIG_EXAMPLE_JPEG_COMPRESSION_QUALITY=80
# 停流功耗看门狗：本项目默认开启（实测 host 停流后空闲电流回落 ~60mA）。
# 选项本身在 main/Kconfig.projbuild 为 default n（对齐官方蓝本、复用中性），此处按本项目实测覆盖为 y；阈值 1000ms（与 Kconfig 默认一致）。
CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF=y
CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF_MS=1000

# 系统/RTOS：对齐同板例程，tick 100Hz→1000Hz，流水线任务调度/超时粒度 10ms→1ms
CONFIG_FREERTOS_HZ=1000
# 运行时统计（供 main_uvc 的 monitor task 用 uxTaskGetSystemState 打印 CPU 使用率 + 逐任务明细）
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
# 让 TaskStatus_t 带 xCoreID，逐任务明细可显示任务所在核
CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y

# 分区表：摄像头固件较大，用内置 large 分区（factory ~1.5MB）；构建后 idf.py size 若超再改 CUSTOM
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y

# 注：CSI 采集设备 CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE 默认 y（且 select ISP），无需显式列。
# 注：ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE（默认 n）是 P4 当 USB host 读外接 USB 摄像头（≠ 本项目的 usb_device_uvc device 输出），本项目用 MIPI-CSI 采集 + usb_device_uvc 输出，故不开（开了还会与 usb_device_uvc 争同一 USB-OTG）。
# 注：CSI PHY 2.5V 由 example_video_common 默认初始化 LDO 提供，无需额外配置。
# 注：CSI 驱动 backup buffer 上游 default y = 默认禁用（同板例程 video_lcd_display 亦如此）；本例程跟随默认（省内存）。若实测丢帧需后备缓冲，显式 CONFIG_ESP_VIDEO_DISABLE_MIPI_CSI_DRIVER_BACKUP_BUFFER=n。
# 注：example_video_common 的 EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR 虽 default y，但其依赖链仅被 esp_cam_sensor/motors/dw9714 select、OV5647 不触发（恒 n），无需配置。
# 注：flash size（16MB）、SPIRAM（模组 PSRAM，含 SPIRAM_MODE / 200M-EXPERIMENTAL 相关说明）等 P4 模组/target 硬件属性见 sdkconfig.defaults.esp32p4（P4 构建时叠加）。
```

> **最终态说明**：以上配置块以当前 `sdkconfig.defaults` 为准（flash size、SPIRAM 等 N16R32 模组/target 硬件属性置于 `sdkconfig.defaults.esp32p4`），可直接复现默认 1920×1080@30 + bulk + monitor。最初 C3 实施时传输模式为 isoc，GPIO0/monitor/bulk 在随后真机调试提交中加入；演进记录见「执行结果」与 spec Q24/Q28/§11。

- [x] **Step 2: 干净重建（保持 blink 激活），使新默认生效**

Run: `cd /workspace && rm -f sdkconfig && idf.py fullclean && idf.py build`
Expected: `Project build complete.`。选了 customized 板 + OV5647 + MJPEG 后，`example_video_common` 改用 `include/boards/customized/` 编译；`SRCS` 仍 blink（app_main 为 blink），但整条视频/UVC 组件链因依赖污染被编译。
（偏差预案：若 kconfig 报某符号 unknown 或组件编译失败，STOP 汇报并核对配置项名。）

- [x] **Step 3: 正向断言关键配置项已写入生成的 `sdkconfig`**

Run: `cd /workspace && grep -E 'CONFIG_(CAMERA_OV5647|CAMERA_OV5647_MIPI_RAW10_1920X1080_30FPS|EXAMPLE_SELECT_CUSTOMIZED_DEV_BOARD|EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN|EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN|EXAMPLE_MIPI_CSI_XCLK_PIN|ESP_VIDEO_ENABLE_HW_JPEG_ENC_VIDEO_DEVICE|ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER|FORMAT_MJPEG_CAM1|FRAMESIZE_FHD|UVC_CAM1_FRAMESIZE_(WIDTH|HEIGT)|UVC_CAM1_FRAMERATE|UVC_CAM1_MULTI_FRAMESIZE|UVC_MODE_BULK_CAM1|EXAMPLE_DISABLE_USB_MSC_STORAGE|SPIRAM|FREERTOS_HZ|FREERTOS_USE_TRACE_FACILITY|FREERTOS_GENERATE_RUN_TIME_STATS|FREERTOS_VTASKLIST_INCLUDE_COREID|PARTITION_TABLE_SINGLE_APP_LARGE)' sdkconfig`
Expected（关键项断言，逐一命中）：
```
CONFIG_EXAMPLE_SELECT_CUSTOMIZED_DEV_BOARD=y
CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN=8
CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN=7
CONFIG_EXAMPLE_MIPI_CSI_XCLK_PIN=-1
CONFIG_CAMERA_OV5647=y
CONFIG_CAMERA_OV5647_MIPI_RAW10_1920X1080_30FPS=y
CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_ENC_VIDEO_DEVICE=y
# CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER is not set
CONFIG_FORMAT_MJPEG_CAM1=y
CONFIG_FRAMESIZE_FHD=y
CONFIG_UVC_CAM1_FRAMESIZE_WIDTH=1920
CONFIG_UVC_CAM1_FRAMESIZE_HEIGT=1080
CONFIG_UVC_CAM1_FRAMERATE=30
# CONFIG_UVC_CAM1_MULTI_FRAMESIZE is not set
CONFIG_UVC_MODE_BULK_CAM1=y
CONFIG_EXAMPLE_DISABLE_USB_MSC_STORAGE=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```
（说明：去掉 grep 尾部 `=` 后，`UVC_CAM1_MULTI_FRAMESIZE` 可命中 `# … is not set`、`SPIRAM_SPEED_200M` 也能匹配；`SPIRAM`/`CAMERA_OV5647` 等词会各带出一批子项行（含 `=y` 与 `# … is not set`，如 `SPIRAM_MODE_HEX`/`SPIRAM_SPEED=200`、`CAMERA_OV5647_AUTO_DETECT`/`DEFAULT_FMT_*`、被显式关闭的 `RAW8_*`/`1280X960` 等）——均属预期，只需确认上表关键项逐一在输出中即可。）

并断言未启用两项（应无 `=y`）：`grep -E 'CONFIG_(ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE|IDF_EXPERIMENTAL_FEATURES)=y' sdkconfig` → 无输出。
（说明：若某配置项名在选定组件版本下与本清单不一致，以 `idf.py menuconfig` 实际项为准核对后 STOP 汇报，见 spec §8 与 D9（QA Q22）。）

- [x] **Step 4: 不单独提交（随 C3 一并提交，见 Task 7）**

---

## Task 4: 追加 `sdkconfig.defaults.esp32p4` 模组/target 硬件（flash + SPIRAM）

**Files:**
- Modify: `sdkconfig.defaults.esp32p4`（追加 flash + SPIRAM + 2 说明注，P4 revision 两项不动。注：SPIRAM 及其说明注为后续「模组硬件 vs 应用」重组迁入、归 C3，见执行结果与 spec §11）

- [x] **Step 1: 在 `sdkconfig.defaults.esp32p4` 末尾追加**

```ini

# 板级硬件属性：本板模组 N16R32 = 16MB flash（与 revision 并列；官方 uvc 例程无此项亦可构建，本板显式设更稳）
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# 大内存：帧缓冲需 PSRAM（N16R32 模组自带 32MB PSRAM；P4 默认 SPIRAM=n，必须显式开）；200M 为 v5.5.4 默认速度
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
# 注：P4 的 SPIRAM_MODE 唯一且默认 HEX（16-line），无需显式配置。
# 注：IDF_EXPERIMENTAL_FEATURES 在 v5.5.4 下 200M PSRAM 已不需要（官方 uvc 例程带此项属其 5.4.x 版本耦合），故本例程不加；若 menuconfig/构建异常再补。
```

- [x] **Step 2: 干净重建并断言 flash size 生效**

Run: `cd /workspace && rm -f sdkconfig && idf.py reconfigure && grep -E 'CONFIG_ESPTOOLPY_FLASHSIZE(_16MB)?' sdkconfig`
Expected: 含 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` 与 `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`。P4 revision 两项仍在（`grep ESP32P4_REV_MIN_0 sdkconfig` → `=y`）。

- [x] **Step 3: 不单独提交（随 C3，见 Task 7）**

---

## Task 5: 追加 `main/Kconfig.projbuild` 的 JPEG 质量项（保留 blink 菜单）

**Files:**
- Modify: `main/Kconfig.projbuild`（在现有 `endmenu` 前追加 `EXAMPLE_JPEG_COMPRESSION_QUALITY`）

- [x] **Step 1: 在 `main/Kconfig.projbuild` 的最后一行 `endmenu` 之前插入以下块**

（JPEG 质量项如下，逐字取自官方 `esp_video/examples/uvc/main/Kconfig.projbuild` 的 MJPEG 段；官方 H264 段 `EXAMPLE_H264_I_PERIOD`/`BITRATE`/`MIN_QP`/`MAX_QP` 亦逐字补回以对齐蓝本、使切 H264 可编译——H264 段较长此处不内联，以官方蓝本 / 实际 `main/Kconfig.projbuild` 为准，详见执行结果「补回 H264 Kconfig」）：

```kconfig

    if FORMAT_MJPEG_CAM1
        config EXAMPLE_JPEG_COMPRESSION_QUALITY
            int "JPEG compression quality (%)"
            default 80
            range 1 100
            help
                JPEG compression quality percentage (1-100).

                Higher values produce better image quality but larger data streams:
                - 90-100: Excellent quality, high bandwidth usage
                - 70-90: Good quality, moderate bandwidth (recommended for UVC)
                - 50-70: Acceptable quality, lower bandwidth
                - 1-50: Poor quality, minimal bandwidth

                For UVC streaming, consider the USB bandwidth limitations
                and host system capabilities when choosing quality level.

                Recommended: 80 for balanced quality and performance.
    endif
```

插入后 `main/Kconfig.projbuild` 结构应为：`menu "Example Configuration"` → `orsource env_caps` → blink 的 `BLINK_*`（choice/config 保持不动）→ **新增 `if FORMAT_MJPEG_CAM1 … endif`** → `endmenu`。（注：本步骤仅插入 JPEG 质量项；H.264 参数块 `if FORMAT_H264_CAM1 … endif` 与 menu 顶层的停流看门狗选项 `EXAMPLE_UVC_IDLE_STREAMOFF`/`_MS` 为后续补入，最终 Kconfig 结构见 File Structure 与执行结果「补回 H264 Kconfig」「Task 13」。）

- [x] **Step 2: 干净重建，验证 Kconfig 语法正确且质量项生效（此时 MJPEG 已在 Task 3 选中）**

Run: `cd /workspace && rm -f sdkconfig && idf.py reconfigure && grep -E 'CONFIG_(EXAMPLE_JPEG_COMPRESSION_QUALITY|BLINK_LED_STRIP|BLINK_GPIO)=' sdkconfig`
Expected: 含 `CONFIG_EXAMPLE_JPEG_COMPRESSION_QUALITY=80`（因 `FORMAT_MJPEG_CAM1=y` 该项可见），且 blink 符号 `CONFIG_BLINK_LED_STRIP=y`/`CONFIG_BLINK_GPIO=51` 仍在（证明 blink 菜单未被破坏）。（本 Task 只改 Kconfig、不改源码，`reconfigure` 即可验证 Kconfig 语法与断言；完整 build 由 Task 7 主验证覆盖。）

- [x] **Step 3: 不单独提交（随 C3，见 Task 7）**

---

## Task 6: 创建 `main/main_uvc.c`（历史初始导入：逐字复制蓝本，暂不激活）

> 本 Task 记录 C3 的初始导入动作：当时文件为 416 行、与蓝本逐字一致；后续真机调试已加入 GPIO0 上电与 monitor，当前最终文件以 File Structure 的描述为准。

**Files:**
- Create: `main/main_uvc.c`（逐字复制自 `esp_video/examples/uvc/main/uvc_example.c`@`002e2c0`）

- [x] **Step 1: 从 Task 2 的 clone 逐字复制到目标文件名**

```bash
cp /tmp/esp-video-components/esp_video/examples/uvc/main/uvc_example.c /workspace/main/main_uvc.c
```
（若 `/tmp/esp-video-components` 已不在，按 Task 2 Step 1 重新 clone 并 `git checkout 002e2c0` 后再 cp。）

- [x] **Step 2: 校验文件正确（行数 + 关键结构，逐字来自蓝本、勿手改）**

Run: `wc -l /workspace/main/main_uvc.c` → Expected: `416`。
关键结构断言（`grep` 命中，证明是 MJPEG-capable 的 uvc 蓝本）：
- `#include "usb_device_uvc.h"`、`#include "uvc_frame_config.h"`、`#include "example_video_common.h"`
- `#if CONFIG_FORMAT_MJPEG_CAM1`（JPEG 路径）+ `control[0].id       = V4L2_CID_JPEG_COMPRESSION_QUALITY;` + `control[0].value    = CONFIG_EXAMPLE_JPEG_COMPRESSION_QUALITY;`
- `void app_main(void)` 内依次调用 `example_video_init()`、`init_capture_video(uvc)`、`init_codec_video(uvc)`、`init_uvc(uvc)`
（等价 shell：`grep -nE 'CONFIG_FORMAT_MJPEG_CAM1|V4L2_CID_JPEG_COMPRESSION_QUALITY|example_video_init|app_main' /workspace/main/main_uvc.c`）

> 说明：`main_uvc.c` 是**传感器无关**的通用 UVC 代码，MJPEG/H264 由 Kconfig 二选一（`#if CONFIG_FORMAT_MJPEG_CAM1 … #elif CONFIG_FORMAT_H264_CAM1`）。Task 3 已配 `FORMAT_MJPEG_CAM1=y`，故默认 H264 分支不参与编译；`main/Kconfig.projbuild` 已备齐 `EXAMPLE_H264_*`（对齐蓝本），切 `FORMAT_H264_CAM1=y` 时 H264 分支可正常编译（MJPEG/H264 双态构建验证、见执行结果）。与 blink plan「逐字取蓝本、不手改」惯例一致。

- [x] **Step 3: 构建验证（`main_uvc.c` 未加入 `SRCS`，不参与编译；仍构建 blink）**

Run: `cd /workspace && idf.py build`
Expected: `Project build complete.`（`main_uvc.c` 此时不被编译，用于确认文件落位不影响现有构建）。

- [x] **Step 4: 不单独提交（随 C3，见 Task 7）**

---

## Task 7: `main/CMakeLists.txt` 切 `main_uvc.c` 默认激活（主验证）

**Files:**
- Modify: `main/CMakeLists.txt`

- [x] **Step 1: 把 `main/CMakeLists.txt` 改为（`main_uvc.c` 激活，其余注释）**

```cmake
idf_component_register(
    SRCS
    # "main.c"
    # "main_hello_world.c"
    # "main_blink_example.c"
    "main_uvc.c"

    INCLUDE_DIRS
    "."
)
```

- [x] **Step 2: 主验证 —— 干净全量构建完整 UVC 例程**

Run: `cd /workspace && rm -f sdkconfig && idf.py fullclean && idf.py build`
Expected: `Project build complete.`，产物 `build/WT9932P4-TINY.bin` 生成、无 "Cannot find source file"/链接未定义符号报错。这是本 plan 的核心 Cloud 验证：`esp_video`/`usb_device_uvc`/`esp_cam_sensor` 拉取、本地 `example_video_common`（customized 板）编译、`main_uvc.c` 链接（`example_video_init`/`uvc_device_init` 等符号解析）全部通过。
（偏差预案：若链接报 `usb_device_uvc` 版本/`tinyusb` 冲突或 UVC 符号缺失，STOP 汇报，见 spec D9。）

- [x] **Step 3: 提交（C3，精确 add 本 Task 涉及的全部实现文件）**

```bash
git -C /workspace add sdkconfig.defaults sdkconfig.defaults.esp32p4 main/Kconfig.projbuild main/main_uvc.c main/CMakeLists.txt
git -C /workspace commit -F - <<'EOF'
feat(main_uvc): :sparkles: 新增 OV5647→ISP→MJPEG→USB UVC 例程并默认激活

- 新增 main_uvc.c（源自官方 uvc_example.c）+ 板级/传感器/管线 sdkconfig 默认 + Kconfig JPEG/H264 编码参数项（对齐官方蓝本、默认激活 MJPEG）
- 采集/输出默认 1920×1080@30 RAW10；sdkconfig.defaults 将 5 档 + UVC 尺寸做成注释块，切注释即换档
- 默认关 ISP_PIPELINE_CONTROLLER（对齐官方 uvc 蓝本、缓解偏黄）、默认 bulk 传输、JPEG 质量显式 80
- Kconfig 备齐 MJPEG/H264 两套参数、main_uvc.c 保留 H264 分支；sdkconfig.defaults 编码格式做成「2 选 1 整组注释」一键切换（同分辨率档风格、切 H264 只需注释/取消注释整组），MJPEG/H264 双态构建均通过
- SPIRAM（模组 PSRAM）与 flash size 等 P4 模组/target 硬件属性置于 sdkconfig.defaults.esp32p4；FREERTOS_HZ、分区表等系统/应用调优留在 sdkconfig.defaults
EOF
```

---

## Task 8: `idf.py size` 体积检查 + 分区判断

**Files:** 无（仅验证；仅当超限才 fallback 新增 `partitions.csv`）

- [x] **Step 1: 查看固件体积**

Run: `cd /workspace && idf.py size`
Expected: 正常输出各段体积；记录 app（bin）大小。

- [x] **Step 2: 判断分区是否够用**

- 若 app 明显 < 1.5MB（大概率）：维持内置 `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`，**本 Task 无改动**。
- 若 app 接近/≥ 1.5MB：**STOP 汇报**（属 spec D14 的 fallback 分支）——确认后再切 `CONFIG_PARTITION_TABLE_CUSTOM` + 新增 `partitions.csv`（本板 16MB，可给 factory 如 4MB），分区表由 sdkconfig 项配置、不改顶层 `CMakeLists.txt`。

- [x] **Step 3: 不提交（无文件改动）。若 Step 2 触发分区 fallback（新增 `partitions.csv` + 改 sdkconfig 项），因 C3 已在 Task 7 提交、无法并入，故追加一个新提交（如 `fix(partitions): :bug: app 超 1.5MB 改用 CUSTOM 分区表`）**

---

## Task 9: 确认未启用 H264 device / experimental（对照官方基线）

**Files:** 无（仅验证；对应 spec D8 + QA Q19 的「H264 device 默认不启用、可切换」结论）

- [x] **Step 1: 断言两项在完整 UVC 构建下均未启用、且 MJPEG 链路已构建成功**

Run: `cd /workspace && grep -E 'CONFIG_(ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE|IDF_EXPERIMENTAL_FEATURES)=y' sdkconfig; echo "exit=$?"`
Expected: 无匹配行（两项未 `=y`）。结合 Task 7 主验证已通过 ⇒ 证明「不启用 H264 device、不启用 experimental」下 MJPEG(JPEG device) 链路可正常构建（官方基线用 SC2336+H264+experimental，本例程默认 OV5647+MJPEG 精简（H264 参数项已备齐对齐蓝本、切 H264 双态构建通过，见执行结果），见 spec D8/§8）。
（偏差预案：若 Task 7 曾因缺这两项而构建失败，则应已在 Task 7 STOP；此处仅复核结论、无需改动。）

- [x] **Step 2: 不提交（无文件改动）**

---

## Task 10: 回归 —— 临时切 blink 构建成功再复位 uvc

**Files:** `main/CMakeLists.txt`（临时改、随后复位；净改动为 0）

- [x] **Step 1: 临时把 `SRCS` 切回 blink**

把 `main/CMakeLists.txt` 的 `SRCS` 改为激活 `"main_blink_example.c"`、注释 `"main_uvc.c"`（其余不变）。

- [x] **Step 2: 干净重建 blink（确认方案 A 依赖引入未破坏其它 demo）**

Run: `cd /workspace && rm -f sdkconfig && idf.py fullclean && idf.py build`
Expected: `Project build complete.`（blink 在引入 uvc 依赖后仍可编译/激活；验证 spec N1「不破坏现有」）。

- [x] **Step 3: 复位为 uvc 激活（恢复 Task 7 的 `main/CMakeLists.txt`）**

把 `main/CMakeLists.txt` 复位为 `main_uvc.c` 激活、其余注释，与 Task 7 Step 1 完全一致。
Run: `cd /workspace && git diff --stat main/CMakeLists.txt`
Expected: 无差异（净改动为 0，说明已正确复位）。

- [x] **Step 4: 不提交（净改动为 0）**

---

## Task 11: 更新 `AGENTS.md` 的「选择运行哪个 demo」章节

**Files:**
- Modify: `AGENTS.md`（demo 列表：把「默认激活」从 blink 改到 uvc，并追加 uvc 条目）

- [x] **Step 1: 把 blink 条目的「**默认激活**」标记去掉**

将 `AGENTS.md` 中 `` - `main_blink_example.c`（**默认激活**）：点亮板载 RGB。…`` 一行的 `（**默认激活**）` 删除（其余描述保留），变为 `` - `main_blink_example.c`：点亮板载 RGB。…``。

- [x] **Step 2: 在 blink 条目之后追加 uvc 条目（列表最后一项，符合"最新的在最后"约定）**

```markdown
- `main_uvc.c`（**默认激活**）：把开发板变成 USB 摄像头（UVC Device）。OV5647 经 MIPI-CSI 采集 RAW10 1920×1080，走 P4 ISP + 硬件 JPEG 压成 MJPEG，默认通过 HUSB bulk 输出；初始实现曾用 isoc，真机发现约 65Mbps 带宽瓶颈后改为 bulk。代码基于乐鑫官方 `uvc_example.c`，并完成 GPIO0 上电与 5 秒周期 monitor 适配；默认配置见 `sdkconfig.defaults`，JPEG 质量可在 menuconfig 调整。Cloud 仅能构建，帧率/画质/功耗需真机验证。
```

- [x] **Step 3: 构建不受影响（AGENTS.md 为文档），提交（此为最初逻辑编号 C4；物理提交中 AGENTS 仅由 C7 定稿（纯 doc-last）、无独立 docs(AGENTS.md) 提交，见「提交约定」与 Self-Review 提交映射）**

```bash
git -C /workspace add AGENTS.md
git -C /workspace commit -F - <<'EOF'
docs(AGENTS.md): :memo: demo 章节纳入 uvc 例程

- 「选择运行哪个 demo」新增 main_uvc.c 条目（OV5647/UVC/MJPEG/1080p30/HUSB/macOS 查看/同向 FPC 警告/时钟依赖模块晶振）
- 默认激活标记从 blink 移到 uvc
EOF
```

---

## Task 12: 文档收尾（spec/plan 初稿已预提交，实现后回填）

**Files:**
- Modify: `docs/superpowers/plans/2026-07-17-uvc-camera.md`（实现完成后回填执行详情）
- （spec 与本 plan 已按序入库，见 Step 1 的提交序列）

- [x] **Step 1: 知悉——文档初稿与 spec 口径修订已由 `d0028c0` 预提交（即最初逻辑编号 C5 本体；物理提交中 spec/plan 归 C7 docs，见「提交约定」与 Self-Review 映射）**

spec 与本 plan 已按序入库：spec 初稿 `45e1735` → 新增本 plan + spec 改名 07-17 + spec 口径修订（PR #4「已知待办」#2/#3/#4 + 关联 Plan：状态行日期拆分为 07-16 定稿 / 07-17 增补 25MHz 专项查证、「第八轮」降级为逐行核对、D12 的「24M input」计数订正、关联 Plan 改「已创建」）`d0028c0` → 第二轮 plan/spec 复审修订 `daa12a3`。这些文档提交即 C5 本体（实际 message 以对应提交为准）。故进入实现时本 Task 通常**无待提交**、可直接跳到 Step 3。

- [x] **Step 2: 实现完成后回填 plan 执行详情并追加提交（若有变更）**

按仓库约定「执行计划后将完成详情回填 plan」：实现全部 Task 后，在本 plan 勾选已完成步骤（`- [ ]`→`- [x]`）、并在文末追加「执行结果」（构建产物 / `idf.py size` / 偏差处理 / 回归等）。然后精确提交：

```bash
git -C /workspace add docs/superpowers/plans/2026-07-17-uvc-camera.md
git -C /workspace commit -F - <<'EOF'
docs(docs/superpowers): :memo: 回填 uvc 例程 plan 执行结果

- 勾选已完成步骤，回填构建/size/偏差处理/回归结果
EOF
```

若实现期间未改动 plan/spec（无回填内容），则本步跳过、无提交。

- [x] **Step 3: 确认工作区干净、无误纳生成物/本地目录**

Run: `cd /workspace && git status --short`
Expected: 无 `??`/未暂存的意外项（`dependencies.lock`、`sdkconfig`、`managed_components/`、`.ref_tmp/`、`.cursor/skills/grilling/` 均不应出现——前二为忽略/未跟踪且不 add，后二在 `.git/info/exclude`）。若出现意外未跟踪项，核对后再决定（勿 `git add -A`）。

---

## Self-Review

### 1. Spec 覆盖率（逐项映射到 Task）

| Spec 条目 | 覆盖 Task |
|---|---|
| R1（main_uvc.c 实现 CSI→ISP→JPEG→UVC，参照蓝本，复用 example_video_common + usb_device_uvc） | Task 2, 6, 7 |
| R2（融入 SRCS 切换、默认激活） | Task 7 |
| R3（依赖：esp_video/usb_device_uvc/esp_cam_sensor + 复制 example_video_common） | Task 1, 2 |
| R4（OV5647 RAW10 1080p30；UVC MJPEG 1080p30） | Task 3（传感器档 + MJPEG/FHD/30） |
| R5（customized 板、SCL8/SDA7、XCLK_PIN=-1） | Task 3 |
| R6（sdkconfig.defaults + .esp32p4 + Kconfig.projbuild 追加 JPEG 项、保留 blink） | Task 3, 4, 5 |
| R7（AGENTS.md demo 章节纳入 uvc） | Task 11 |
| R8（提交 spec 与 plan） | Task 12 |
| N1（不破坏现有其它 demo/结构） | Task 10（回归 blink） |
| N2（默认 SRCS=main_uvc.c，开箱 build） | Task 7 |
| N3（CI 只构建激活 demo、默认即 uvc） | Task 7（不改 CI，默认激活即验证） |
| N4（MJPEG 三端通用） | Task 3（FORMAT_MJPEG_CAM1） |
| D1（OV5647） | Task 3 |
| D2（UVC Device） | Task 6, 7 |
| D3（MJPEG） | Task 3 |
| D4（1080p30 + 默认 bulk；isoc/降级备选） | Task 3（FHD/30/单档/bulk）；isoc→bulk 演进见执行结果 |
| D5/D6/D7（方案 A / 默认激活 / 命名 main_uvc.c） | Task 6, 7 |
| D8（sdkconfig 精简：不选 SC2336、默认 MJPEG choice、H264 device 默认不启用但 Kconfig 备齐可切换、不加 experimental） | Task 3, 9 |
| D9（依赖引入 + ^ 范围 + 不提交 lock + esp_cam_sensor 显式） | Task 1 |
| D10（复制 example_video_common、选 customized 板） | Task 2, 3 |
| D11（QA 附录） | spec §10（无需 Task） |
| D12（时钟 XCLK_PIN=-1 依赖模块晶振 + 25MHz 兼容） | Task 3（XCLK_PIN=-1）；运行时项记于执行前必读 |
| D13（UVC 配置细节 + JPEG 质量项到 Kconfig.projbuild） | Task 3, 5 |
| D14（flash 16MB + SINGLE_APP_LARGE + size 验证 fallback） | Task 4, 3, 8 |
| §8 测试与验证策略（build 主验证/配置断言/size/回归） | Task 1-3, 7, 8, 10 |
| §13 / Q36（停流功耗修复设计，可选特性 `EXAMPLE_UVC_IDLE_STREAMOFF`，2026-07-21 新增） | Task 13 |

结论：R1–R8、N1–N4、D1–D14、§8 均有对应 Task；§13/Q36（2026-07-21 新增可选特性）由 Task 13 覆盖；D11 属纯文档、无需实现步骤。无遗漏。

### 2. 占位符扫描

已检查：无 "TBD/TODO/implement later/待补充"；Task 3 配置块为当前可复现最终态；Task 6 明确标注为初始蓝本导入历史，当前 `main_uvc.c` 已演进为 GPIO0 上电 + monitor 的本板适配版本；所有 `Run`/`Expected` 均给出具体命令与预期。

### 3. 配置/类型一致性

- 配置项名已对源码与当前 `sdkconfig.defaults` 核对：OV5647 1080p、ISP/JPEG、显式 WIDTH/HEIGT/FRAMERATE、`UVC_MODE_BULK_CAM1`、单帧尺寸、USB MSC 禁用，以及 monitor 所需 3 个 FreeRTOS 统计项均一致；保留上游 `FRAMESIZE_HEIGT` 的既有拼写。
- 板级项 `EXAMPLE_SELECT_CUSTOMIZED_DEV_BOARD`/`EXAMPLE_ENABLE_MIPI_CSI_CAM_SENSOR`/`EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN`/`SDA_PIN`/`XCLK_PIN` 与 `example_video_common` Kconfig 实测一致（customized 板默认恰为 8/7/-1）。
- 依赖版本一致：Task 1 的 `^2.3.0`/`^1.3.1`/`^2.3.0` 与 spec D9、Registry 现状一致；`example_video_common` 复制版本（commit `002e2c0`，`idf_component.yml`=2.3.0）与 Registry esp_video 2.3.0 匹配。
- Task 13 新增项一致：Kconfig `EXAMPLE_UVC_IDLE_STREAMOFF`(bool default n) / `EXAMPLE_UVC_IDLE_STREAMOFF_MS`(int default 1000 range 200 60000)；状态/并发原语 `s_stream_paused`(volatile bool 初值 true) / `s_fb_seq`(volatile uint32) / `s_stream_gen`(volatile uint32) / `s_stream_lock`(`SemaphoreHandle_t`，需 `freertos/semphr.h`) —— 与 spec §13 及 Task 13 代码块逐一核对一致。
- Task 13 并发不变量核对（除名称外，另核对执行性/时序，避免仅比对符号名而漏掉编译/并发缺陷）：① **函数定义顺序**——`stream_off_locked`/`stream_watchdog_task` 置于 `video_stop_cb`（当前基线约 L407，在 `video_fb_get_cb`/`video_fb_return_cb` 之前）之前，满足「`video_stop_cb` 调 helper、`stream_watchdog_task` 调 helper、`app_main` 建 watchdog」三处「先定义后使用」（Step 3；否则宏开构建报隐式声明/类型冲突）；② **mutex 生命周期**——`s_stream_lock` 在 `init_uvc()`（内部 `uvc_device_init` 启动 TinyUSB/UVC 回调任务）之前创建，杜绝回调撞 NULL 锁（Step 7a）；③ **generation 基线刷新**——看门狗第二分支以 `s_fb_seq != last_seq || s_stream_gen != last_gen` 刷新基线，保证「STREAMON 后零帧」的流 gen 变化也能重置计时、不致 double-check 永久跳过（Step 3）；④ **锁配对无泄漏**——`video_start_cb` 唯一早退与结尾各一次 give、其余失败点 `ESP_ERROR_CHECK`/`assert` abort 不返回（Step 5）；⑤ **副作用不入 assert**——`xTaskCreate` 裸调用后单独 `assert` 返回值（关断言时任务仍建，Step 7b）。
- 提交映射（物理 7 提交、与 `git log`/PR 一致）：C1↔Task1、C2↔Task2、C3↔Task3-7（主体+配置）、C4↔GPIO0 修复、C5↔monitor、C6↔Task13(停流看门狗)、C7↔文档(Task11 AGENTS + Task12 spec/plan + Task13 文档合并入此、未单列 C8、doc-last)；§13/Q36（停流功耗修复）→ Task 13。每个提交后代码可构建。

---

## 执行方式选择（Execution Handoff）

Plan 已完成并保存到 `docs/superpowers/plans/2026-07-17-uvc-camera.md`。两种执行方式：

1. **Subagent-Driven（推荐）** —— 每个 Task 派新 subagent 实现、Task 间两阶段 review，快速迭代。REQUIRED SUB-SKILL：`superpowers:subagent-driven-development`。
2. **Inline Execution** —— 在本会话按 `superpowers:executing-plans` 批量执行、按 checkpoint 复审。REQUIRED SUB-SKILL：`superpowers:executing-plans`。

选哪种？

> **执行记录**：本 plan 已于 2026-07-17 按 **Subagent-Driven Development** 执行完成（上方各 Step 复选框已勾选），详见下方「执行结果」。

---

## 执行结果（2026-07-17 · subagent-driven-development 回填）

**执行方式**：superpowers subagent-driven-development —— 按提交单元派 implementer subagent 实现（不传模型 slug、用父模型），核心单元（Task 2 / Task 3-7）辅以独立 spec 合规 reviewer subagent + controller 的 git/构建 ground-truth 核验；简单单元（配置/验证/文档）由 controller 直接执行并核验。

**提交序列**（⚠️ 早期 `03374b7`/`d18b89f`/`0cfccc6`/`cf6b0d4`/`76e425e` 等为 **squash/rebase 前**的历史哈希、现已不可达；当前分支实际基线 = 与 `dev` 的 merge-base = `f7d1dc2`）。**当前实际可达序列 = 7 个物理提交 C1–C7**（见上「提交约定」的物理提交清单；hash 随 amend/rebase 变动、以 `git log --oneline f7d1dc2..HEAD` 为准，doc-last）。`Co-authored-by: 远哥制造` 由 cloud 平台 hook 自动补加，当前 7 提交均带此 trailer。**下方 C1–C5 列表为 2026-07-17 回填时的旧编号快照（hash 已不可达、结构为当时逻辑分组，仅存历史；当前物理 7 提交 C1–C7 以上「提交约定」为准）**：
- **C1** `03374b7` `build(deps)`：`main/idf_component.yml` 追加 `esp_video`/`usb_device_uvc`/`esp_cam_sensor`
- **C2** `d18b89f` `feat(example_video_common)`：vendored 复制官方组件（14 文件，与官方 `002e2c0` 逐字一致）
- **C3** `0cfccc6` `feat(main_uvc)`：sdkconfig/Kconfig/`main_uvc.c`/激活（主验证通过）
- **C4** `cf6b0d4` `docs(AGENTS.md)`：demo 章节纳入 uvc
- **C5** 本提交 `docs(docs/superpowers)`：回填 plan 执行结果

**各 Task 结果**：
- **Task 1（C1）**：依赖解析/拉取成功——`managed_components/` 含三直接依赖 + 传递依赖（`esp_ipa`/`esp_h264`/`esp_sccb_intf`/`cmake_utilities`/`usb_host_uvc`/`tinyusb`），`usb_device_uvc 1.3.1` 解析 `tinyusb 0.19.0~3`，无 `esp_new_jpeg`（符合预期）；首次构建 `Project build complete.`。
- **Task 2（C2）**：`components/example_video_common/` 14 文件复制完整、`diff` 与官方 `002e2c0` 逐字一致；构建 `Project build complete.`，连带拉取 `esp_new_jpeg 1.0.2`；env_caps 双 `orsource` 告警**本环境未触发**（更干净）。
- **Task 3-7（C3）**：`sdkconfig.defaults` + `sdkconfig.defaults.esp32p4`（flash 16MB + SPIRAM，后者为 M1 重组迁入）+ `main/Kconfig.projbuild`（JPEG 质量项、保留 blink 菜单）+ `main/main_uvc.c`（416 行、除 N-3 移除一处多余分号外与蓝本逐字一致）+ `main/CMakeLists.txt`（切 `main_uvc.c` 默认激活）；**主验证 `Project build complete.`，产物 `build/WT9932P4-TINY.bin` = 543120 字节**；配置断言全命中（传感器仅 1080p、其余 4 档 `=n`；H264 device/experimental 未启用）；独立 spec reviewer 重建复现通过 ✅。
- **Task 8**：`idf.py size` Total image 542744 字节（Flash 447230、DIRAM 18.14%）——远 < `SINGLE_APP_LARGE` factory 1.5MB，**无需分区 fallback**。
- **Task 9**：`CONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE` 与 `CONFIG_IDF_EXPERIMENTAL_FEATURES` 均未 `=y`；结合主验证 ⇒ 精简（仅 MJPEG）配置下链路可构建。
- **Task 10**：回归——临时切 blink `Project build complete.`（依赖引入未破坏其它 demo，守住 N1），复位 uvc 净改动 0；复位后重建 uvc `Project build complete.`。
- **Task 11（C4）**：`AGENTS.md`「选择运行哪个 demo」新增 uvc 条目、默认激活标记从 blink 移到 uvc。

**中途遇到的问题**：无。Task 1-11 全程构建均 `Project build complete.`、配置断言与 spec review 全部通过，未遇到需修复的偏差（故无「修复合并至对应提交」的动作）。唯一与 plan 预告不同的观察：env_caps 双 `orsource` 的重复定义告警在本环境**未出现**（更干净、非问题）。

**初始 Cloud 交付时留待真机验证（历史快照）**：当时尚未验证 OV5647 25MHz、FHD isoc 带宽、CSI LDO 与 FPC；后续已完成 GPIO0 上电/出图并由 isoc 切 bulk，现状见后续 2026-07-18 记录与 spec §7/§11。

**真机调试记录（2026-07-18，首次联调）**：① **FPC 方向纠正**——实测「同向」为正确连接、「反向」致 3V3/GND 反接触发 brownout（LED 变暗、电感发烫、有烧毁风险），本 plan/spec/AGENTS 原「反向 FPC」已全数改为「同向」；② **摄像头完好**——OV5647 模块在树莓派 5 上验证正常出图，排除损坏；③ **代码无误**——复核采集/ISP/UVC 链路未见导致 detect 失败的代码问题；④ **根因收敛本板 J2 侧**——同向接线下 SCCB 仍无响应，待查 power-enable(CAM_IO0/Pin11)/CSI 供电(实测 3V3=Pin15、GND=Pin1/4/7/10)/FPC 接触/模块晶振(XCLK)；⑤ **brownout(BOD)**——ESP32 全系（含 P4）内置欠压检测，供电跌破阈值即复位/中断以防低压误动作。详见 spec §11 修订记录（2026-07-18）。

**真机点亮成功（2026-07-18）**：根因=本板 J2 为「倒序」树莓派线序，Power-Enable(CAM_IO0)=J2 Pin5=IO0(GPIO0) 从未被驱动；修复=`app_main()` 开头拉高 GPIO0 + 延时 50ms 再 `example_video_init()`（commit `fix(main_uvc)`）。真机日志 `Detected Camera sensor PID=0x5647`、UVC Mount 出图，点亮成功；FPC 用正向/同向。**遗留待调优**：① 画面偏黄（AWB/CCM，ISP IPA 调优方向）；② 1080p 帧率偏低（可切 OV5647 1280×960@45 / 800×800@50 / 800×640@50 / 800×1280@50 更低档并同步 UVC 尺寸，或 bulk 传输 / 降 JPEG 质量）。注：采集档与 UVC 尺寸须成对一致（`video_start_cb` 三处共用 width×height）。

**画质/帧率首轮调优（2026-07-18）**：① 切 1280×960@45 RAW10（提帧率、保画质），`sdkconfig.defaults` 5 档 + UVC 尺寸注释块化（切注释即换档 + `rm sdkconfig` 重建）+ bulk / JPEG 质量注释开关；② 默认关 `ISP_PIPELINE_CONTROLLER`（对齐 uvc 蓝本、试缓解偏黄，代价是无自动 AE/AWB）；③ RAW10(10bit 高画质) / RAW8(8bit 高帧率) 区别补入 spec。构建通过（~525KB）。

**性能监视打印 + 偏黄结论（2026-07-18）**：① main_uvc.c 加 monitor task 每 5s 打印（fps 换算为每秒帧率）+ CPU%（`uxTaskGetSystemState` 双核 IDLE 反推）+ 内存（INT/PSRAM free/min）+ 逐任务明细（各任务 编号/核/状态/优先级(当前+基础)/CPU%/栈剩余/句柄/栈基址；列排版参考官方 vTaskList、CPU% 算法参考官方 real_time_stats 示例即两次采样差分除以核数），sdkconfig 开 runtime stats + `FREERTOS_VTASKLIST_INCLUDE_COREID`（核信息）；开销可忽略（默认每 5s 采样、fps 换算每秒、组间空行分隔；仅上下文切换更新计数器）；② 偏黄关 IPA 后无改善、**非传感器上限**（树莓派上颜色正常），系白平衡（AWB/CCM）默认不准，后续走自定义 IPA JSON / 手动 AWB 增益。

**监视排版 + USB 可配项（2026-07-18）**：① monitor 逐任务表前加 `====` 分割行、与总览行区隔；② USB 设备信息可配项：VID/PID/厂商/产品名/序列号 = `CONFIG_TUSB_*`（Kconfig 可改）；`bcdDevice`/`bMaxPower` 硬编码于库 `usb_descriptors.c`；链接速度/连接类型由硬件决定、不可改。

**分割线对齐 + 默认切 bulk（2026-07-18）**：① monitor 分割线对齐表格宽度(75)、表头末列 %-10s 对齐 %p；② UVC 传输默认 isoc→bulk（`CONFIG_UVC_MODE_BULK_CAM1=y`）——因 isoc 每微帧仅 1 包(1023B)、约 65Mbps 把 1280×960 限到 ~23fps，bulk 可占满总线冲更高帧率。

**真机问答汇总（2026-07-18）**：usb_phy UTMI 告警是否需修 / 实测 fps 瓶颈(isoc 带宽) / `fps=0` 含义(host 未拉流) / RV32(host 解码格式) vs RAW10(sensor 端) / JPEG 质量默认 80 且不影响偏黄 / 逐任务表各任务含义——均以 QA 形式记入 spec §10（Q27–Q35，含 usb_phy 告警、帧率瓶颈、RV32vsRAW10、JPEG/偏黄；及耗电非 umount、bulk 后 ~21fps 串行流水线瓶颈、Flash/PSRAM/CPU 时钟速率）。

**停流功耗根因 QA + 默认分辨率改最高档（2026-07-18）**：① systematic-debugging 定位关流/断开后功耗不回落根因（默认 bulk 无 alt-setting、host 停流无 USB 事件、`tud_video_n_streaming` 对 bulk 恒 true、`tud_umount_cb`/采集任务均无 STREAMOFF），补 spec **Q36** + **修正 Q33**（原「常开正常」为误判）；修复 A/B/C/D 待定。② `sdkconfig.defaults` 分辨率 5 档按面积高→低重排，默认改回 **1920×1080@30 最高档**（回归 D4；spec §5/plan config 本就是 1080p 蓝图）。

**ISP IPA 默认关：实测对齐（2026-07-18）**：用户复测确认**夜间室内关 IPA(`ISP_PIPELINE_CONTROLLER=n`) 比开(=y)明显改善偏黄**（更新此前「关 IPA 无改善」的观察、系光照环境差异）；据此 spec §5/§7 表/plan config 三处 `=y`→`=n` 对齐（`sdkconfig.defaults` 早已 =n）、注释写入实测理由。根治白平衡仍走自定义 IPA JSON / 手动 AWB。

**参考资料 + 停流修复实施建议（2026-07-21）**：三处外部一手来源（Laurent Pinchart 的 linux-usb 邮件、UVC 1.1 §2.4.3、esp32-camera #33）收录至 spec **§12 参考资料**；spec Q36 末尾补停流功耗修复「实施建议」——USB 供电收益有限可选 D，电池/挂机再走方案 B（用 Kconfig 宏 `EXAMPLE_UVC_IDLE_STREAMOFF` default n 整体包裹、默认关=零噪声零风险、启用时拉长暂停 delay + 翻转才打 log 压噪），不选 C（override 整组件对个人例程过度工程）。

**Task 13 实施完成（2026-07-24 · subagent-driven-development）**：按 spec §13 / plan Task 13 Step 1–10 实现可选停流看门狗（`EXAMPLE_UVC_IDLE_STREAMOFF`）。
- **构建（Cloud 判据全绿）**：宏关 `idf.py build` 通过、`Total image size` **529626 B**（= 修改前基线；新增码全在 `#if` 内被预处理器剔除、宏关零运行时影响）；宏开通过、**530410 B**（+784 B，看门狗/helper/mutex/插桩确实编入）、**无新增警告**；宏开 `sdkconfig` 双 `grep` 断言命中（`CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF=y` + `_MS=1000`）。
- **两轮非只读子代理复审**（spec 合规 + code quality，均不传模型 slug）：spec 合规**全绿**（Kconfig + 13 处插桩逐字忠实设计）；code quality **无 Blocker/Major**（锁配对、死锁、看门狗状态机、generation double-check、uint32 回绕、跨核可见性均核实正确）。
- **采纳的修复（M1，合并入 C6 代码提交）**：把 `EXAMPLE_UVC_IDLE_STREAMOFF`/`_MS` 移出 `if FORMAT_MJPEG_CAM1` 门控、置于 menu 顶层——该特性与编码格式无关（STREAMON/STREAMOFF 对 MJPEG/H264 均适用），原门控会在未来支持 H264 时使省电项静默失效；顺带补 `_MS` help 提示（N1）。M1 后重跑宏关/宏开构建、体积不变（529626/530410）。
- **权衡保留（M2/N2，文档说明）**：`fb_get` 暂停守卫 `vTaskDelay(1000ms)` 未改短——改短会使暂停期组件 `Failed to capture` 超「≤1 行/秒」压噪标准；恢复首帧最多滞后约 1s 属窄竞态（bulk 主路径停流期 `video_task` 卡 `tx_busy` 不调 `fb_get`、通常不触发）。已写入 spec §13 压噪段。
- **提交与历史（doc-last）**：Task 13 代码为 **C6**（`feat(main_uvc): :sparkles: …`，含 M1/N1）；Task 13 文档（本条 + spec §13 回填 + M1/M2/N2）**合并进原 docs 提交（C7）并保留其原始信息**（未单列 C8），并用 `reset`+`cherry-pick` 把该 docs 提交**重排到 PR 最后**（代码 C6 在前、superpowers 文档 C7 殿后）。
- **真机（功耗已实测达成）**：停流后功耗 135mA→**~60mA** 回落（用户实测）；PROBE→COMMIT 恢复、日志噪声、suspend→resume 与 isoc 行为仍待验证（Cloud 无硬件，见 spec §13「验证」）。

**真机实测后调优（2026-07-24 · 同日续）**：用户真机实测确认「开启看门狗后停流可回落 ~60mA」，据此调整并按语义归位到对应提交：
- **默认由关改为开**：选项本身 Kconfig 保留 `default n`（中性/复用友好），由 `sdkconfig.defaults` 追加 `CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF=y` 覆盖（同 `SELECT_CUSTOMIZED_DEV_BOARD` pattern）→ 归 **C6**（与看门狗代码同提交）。默认构建产物随之由宏关 529626 B 变宏开 530410 B。
- **阈值 `_MS` 1500→1000ms**（Kconfig `default` 与 `sdkconfig.defaults` 均 1000）：回落更快（~1.0–1.2s）、误判裕度仍足（30fps 档 ≈30 帧空窗）→ 归 **C6**。
- **`JPEG_COMPRESSION_QUALITY` 显式 80**：`sdkconfig.defaults` 由注释示例 `# =60` 改为显式 `=80`（= Kconfig 默认、对齐显式声明风格）→ 归 **C3**（基础画质配置）。
- **C6 提交信息**同步改为「默认开」。物理提交序列：C3(`sdkconfig.defaults` JPEG) / C6(Kconfig + `main_uvc.c` + `sdkconfig.defaults` 看门狗默认开) / C7(docs 殿后)。
- **复验**：`rm sdkconfig` 默认构建 = 宏开 530410 B、`sdkconfig` 断言 `UVC_IDLE_STREAMOFF=y` + `_MS=1000` + `JPEG=80` 命中；手动关宏 = 宏关 529626 B；两态无警告。

**问题3 确认（USB 拔线检测，2026-07-24）**：据开发板原理图 + WT0132P4-A1 规格书确认「拔 HUSB 不打印 `UN-Mount`」的根因——本板两个 Type-C 口 VBUS 经 MOSFET OR-ing 合并、无 VBUS→GPIO 监测，P4 USB-OTG 无法内部检测拔线，组件 `usb_phy_init` 未配 `otg_io_conf`。**选定方案 A**（接受现状：默认开的停流看门狗已在拔线后 ~1s 打印 `UVC stream paused (STREAMOFF)` + 功耗回落，应用层已覆盖「拔掉有反应」需求）；方案 B（硬件飞线 J4 VBUS 分压→空闲 GPIO + override `usb_device_uvc` 配 `vbus_monitor_io`）详录于 spec **Q40** 供参考、未实施（动硬件 + 违背不改组件原则）。详见 spec Q40 与 §11。

**第 3/4 轮 PR 复审收敛（2026-07-25 · grilling 逐项定级后执行）**：12 项发现全部核实真实，但两个 Major 均非代码缺陷、降级为文档——
- **MAJOR-01（恢复）**：host 重开 PROBE→COMMIT 恢复出图经 MBP/VLC **真机实测正常**；spec §7/§13 由「待验证」改「已达成」、AGENTS「自动恢复」保持。
- **MAJOR-02（帧率）**：默认 1920×1080@30 device 侧**实测约 15–16fps**（monitor `perf|fps` 稳定值），瓶颈为串行取帧/编码流水线（CPU 仅 ~26%、双核 IDLE ~72%、非算力/内存/USB）；新增 spec **Q41**、AGENTS 补注。
- **代码（仅 1 项，归 C5）**：MINOR-02 monitor 建任务补 `assert(mon_ok==pdPASS)`（与看门狗一致）；`assert` 在 `#if` 外、宏关宏开各 +64 B → **宏关 529690 B / 宏开 530474 B**（看门狗净增仍 784 B、宏关零运行时论述不变）；双态 `idf.py build` 均过、无新增警告。MINOR-01/NIT-01（monitor fps/CPU% 诊断精度）仅记 spec Q41、不改码。
- **文档一致性**：MINOR-03（Q36 物理断线改条件句 + 引 Q40）、MINOR-04（Step 8/9b 注默认已翻转宏开）、MINOR-05（spec §5 / 本 Task 3 配置块补显式 `JPEG=80` 与看门狗 `y`/`1000`）、NIT-04（C3「逐字」补「除 N-3 外」）、NIT-05（File Structure 逻辑号 vs 物理号 C7）。
- **commit message**：reword C5（补逐任务表 / `VTASKLIST_INCLUDE_COREID` / assert）；搭 `git rebase -i HEAD~3` 顺带完成。物理提交仍 C1–C6 + C7(docs)；本轮文档随 C7。

**第 5 轮复审修复（2026-07-25 · 结构/命名/doc-last，搭 `git rebase -i HEAD~5`）**：
- **Nit-2**：AGENTS 仅在 C7 定稿，移除 C3 对 AGENTS 的改动（纯 doc-last）。
- **Nit-4**：C5 message 去除对尚未引入的「看门狗」前向引用。
- **Nit-5**：AGENTS 1080p 帧率统一为 15–16fps。
- **Nit-6**：GPIO0 电源使能脚字面量 `0` → 宏 `CAM_PWR_EN_GPIO`（归 C4）。
- **配置归位（按「模组硬件 vs 应用」）**：`SPIRAM` 与 flash size 等 N16R32 模组/P4 硬件属性置于 `sdkconfig.defaults.esp32p4`（`SPIRAM` 归 C3）；`FREERTOS_HZ`/运行时统计（`USE_TRACE_FACILITY`/`GENERATE_RUN_TIME_STATS`/`VTASKLIST_INCLUDE_COREID`）/分区表等系统/应用调优、UVC 配置与 6 条说明注均留 `sdkconfig.defaults`（`FREERTOS_HZ`/分区表→C3、运行时统计→C5）。配置集合不变 → 固件体积不变（GPIO 宏等价、配置仅换文件）。

**第 6 轮复审修复（2026-07-25 · 最终态一致性）**：
- **MINOR-3（文档残留）**：Kconfig 看门狗 help「功耗/恢复需真机验证」→「已实测达成、剩 suspend→resume/isoc 边界」（C6）；`sdkconfig.defaults` blink 注反映现默认 UVC + H264「待验证删除」→「已验证（构建 + 真机出图）」（C3）；plan/spec 摘要（C3 不含 AGENTS、SPIRAM 归 `.esp32p4`、Task 13 功耗/恢复已达成、§5 R6 补 SPIRAM）。
- **MINOR-1（记录，不改码）**：跨任务共享 `s_uvc_frame_count`/`s_fb_seq`/`s_stream_paused`/`s_stream_gen` 用 `volatile` 而非 C11 atomic 的取舍与正确性依据（对齐 32 位 lock-free + `s_stream_lock` 串行 + 锁内 double-check）记入 spec §13⑦，并修正 Q41 fps 精度表述。
- **Nit-2**：C6 message 去除对 spec §13 的前向引用（改「设计随 C7 文档化」）。
- **Major-1（in-flight 竞态）**：确认 spec §13③ 已完整记录（(b)(c)(d) + 7 处 `ESP_ERROR_CHECK` + YAGNI）；本轮补 plan 剩余风险④与 PR 正文的显式披露。（2026-07-25 追加机理可视化与根治方向：见下方「Major-1 …机理澄清」段与 spec Q42–Q44 / §13 两图；方案 B 决定另开新 PR、本 PR 维持方案 A。）
- **MINOR-2 未改**：`^` 依赖不可复现 / `dependencies.lock` 未 gitignore，经权衡维持现状（项目既有取舍）。
- 均文档/注释/message 改动，固件不变（宏开 530490 B / 宏关 529706 B）。

**Major-1（in-flight 竞态）机理澄清 + 根治方向（2026-07-25 · brainstorming/grilling，纯文档）**：经用户提问深化，明确并记入 spec——
- **两条 `s_fb_seq` 停滞路径**：host 停拉流时 `video_task` 卡在组件 `tx_busy` 循环（`usb_device_uvc.c` L166-169）、不调 `fb_get`，STREAMOFF **安全**（降功耗主路径）；仅「传感器 hang / suspend / commit 恰命中 in-flight `fb_get`」异常叠加才触发 Major-1（spec §13③(b)、新增 Mermaid + Q42）。
- **外部对照**：官方 `esp_video/examples/uvc/uvc_example.c`（本例 416 行蓝本）同源隐患、不修；乐鑫产品级 esp_capture（esp-gmf）以 `VIDIOC_S_DQBUF_TIMEOUT` + 查 `ioctl` 返回值规避——即根治方案 B 的现成先例（spec Q43、§12）。
- **根治方向与决策**：三方案 A（现状+披露）/ B（DQBUF 超时+优雅错误处理，推荐）/ C（引用计数握手，不推荐）对比记入 spec Q44。**决定本 PR 不实现方案 B**（改主数据路径 `fb_get`、须真机回归、动 C3 干净蓝本基线），当前 PR 维持方案 A（§13③ 已知边界 + PR 正文披露）；**方案 B 另开新 PR**。
- 本轮纯文档（spec §10 Q42–Q44 / §11 / §12 / §13 两图、本 plan 执行结果与剩余风险④），固件不变。

**提交卫生：monitor 注释修正归位到 C5（2026-07-25 · `git rebase -i`）**：monitor 三处失实注释（「过去 1 秒/每秒」→「过去 5 秒/每 5s」）本由 C5（新增 monitor）引入、原在 C6（看门狗）「顺带修正」——逻辑上应随 C5 一次写对。故归位到 C5、从 C6 剥离（含 message 去掉「顺带修正 monitor…」行）；`main_uvc.c` 最终内容与体积不变、仅改动归属。

**补回 H264 Kconfig、对齐官方蓝本使 H264 可切换编译（2026-07-25 · 用户「不能一半实现」要求）**：`main/Kconfig.projbuild` 补回官方蓝本 `if FORMAT_H264_CAM1` 块（`EXAMPLE_H264_I_PERIOD`/`BITRATE`/`MIN_QP`/`MAX_QP`、逐字、归 C3，置于 MJPEG 块与看门狗块之间）；`sdkconfig.defaults` 的 H264 device 注释改为「切 H264 三步」切换说明（默认仍 MJPEG、device 默认不启用以省体积、归 C3）。`main_uvc.c` 不改（H264 分支本就逐字保留蓝本）。**双态 Cloud 构建验证**：MJPEG（默认）与 H264（`FORMAT_H264_CAM1=y` + `ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE=y`）`idf.py build` 均 `Project build complete.`（H264 态 bin=0x86c80=552064，此为编码器 device 归组前、两 device 都开值，归组后 H264 精简为 0x80350=525136；编译 `esp_video_h264_device.c` + `main_uvc.c` H264 分支）；默认 MJPEG 固件不变（H264 块 `if FORMAT_H264_CAM1` 默认不激活、零影响）。文档 D8/§5/§7/§8/Q19/§11 + AGENTS 同步由「删 H264/仅 MJPEG」改为「备齐 H264、默认 MJPEG、切 H264 可用」。（2026-07-25 追加：`sdkconfig.defaults` 编码格式进一步做成「2 选 1 整组注释」一键切换块——同分辨率档风格，切 H264 只需注释 MJPEG 组 + 取消注释 H264 组，spec §5/plan 镜像同步、双态构建复验通过。）

**编码器 device 归入编码格式档、与格式互斥（2026-07-25）**：`HW_JPEG_ENC_VIDEO_DEVICE` 由视频管线区移入编码格式档 [MJPEG] 组、`HW_H264_VIDEO_DEVICE` 在 [H264] 组（归 C3）——切格式时编码器 device 随组开关、互斥（不再像官方蓝本两个都开）。实测 bin：MJPEG 530864 / H264 精简(关 JPEG enc) 525136 / H264 官方式(两开) 552064，即调整后 H264 省 ~26KB。二者只影响固件体积（+少量启动 RAM）、不影响运行时帧率/CPU（不用的 device 不 open/STREAMON、不跑硬件）。spec §5 镜像/必要性表/§11 同步。

**会话问答沉淀为 QA（2026-07-25 · 纯文档）**：回溯本会话「一、配置项详解」以降的技术问答，在 spec §10 新增五条——Q45（`ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE` 是 esp_video 的 USB **host** 采集源、勿与本项目 `usb_device_uvc` 的 device 输出混淆、三类 `ENABLE_*_VIDEO_DEVICE` 并列独立）、Q46（MJPEG 模式关 `HW_JPEG_ENC` 致 `init_codec_video` `assert(fd>=0)` 崩溃的根因＝编码器 device 与格式须严格配对，可选 `#error` 防呆）、Q47（V4L2 JPEG 编码 device 仅硬件版；软件 `esp_new_jpeg` 走 `example_encoder.c` 的 `SELECT_JPEG_HW_DRIVER` 路径、本项目不调用被 `--gc-sections` 丢弃）、Q48（`FORMAT_MJPEG_CAM1` 的 `CAM1`＝第 1 路、`usb_device_uvc` 最多双摄 `UVC_SUPPORT_TWO_CAM`/`index[0,1]`、本项目单路因单 sensor＋单 OTG＋带宽）、Q49（用 monitor 的 `TinyUSB`/总 CPU∝码流字节 为主判据 + PSRAM 参考帧占用 为辅判据 区分当前跑 MJPEG 还是 H264）；并给 Q25 补一处辨析（`ISP_PIPELINE_CONTROLLER`＝IPA 自动调优控制器·默认关，须与必开的 ISP 处理节点 `ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE`＝RAW→RGB/YUV 区分）；spec §11 追加记录、追溯注更新 Q1–Q49。纯文档、固件不变。

**第 2 批 QA 沉淀 + 一致性订正（2026-07-25 · 纯文档）**：经单会话 QA 双向审查采纳——spec §10 **新增** Q50（自定义 USB 描述符 + 硬编码接口名 `iInterface`="UVC CAM1"）、Q51（brownout detector）、Q52（RAW10 vs RAW8）、Q53（H264 与停流看门狗交互）、Q54（FPC 正/反向与反接后果）；**订正** Q9/D8 experimental「待验证删除」→「已验证不需」、Q44 加方案 B 消歧注、Q13 加 Q26/Q38 演进指针；**复核保留** Q37 与 Q34/Q41、Q24/Q28（不删不合并）。追溯注更新 Q1–Q54。纯文档、固件不变。

**PR 复审文档精度收敛（2026-07-25 · 纯文档）**：采纳全量 PR 复审的 Minor + N1——spec §13「成功标准」的「恢复出图」由「预期」补注「（真机实测已达成）」（对齐全文口径）、AGENTS「反向接烧毁摄像头」→「有烧毁风险」（对齐 Q54/§7）。纯文档、固件不变。

**复审残留收敛（2026-07-25 · 纯文档）**：spec D1「反向接会电源反接烧毁」→「有烧毁风险（短时实测未损坏）」（上轮 N1 同类漏改）、本 Task Step 10 的 C6 提交示意块 body 同步为实际 C6 message（补「默认 1000ms/实测回落 ~60mA」、「设计随 C7 文档化」）；Task 7 Step 3 的 C3 提交示意块 body 亦同步为实际 C3 message（「逐字复制→源自」「仅 MJPEG→备齐 H264」）。纯文档、固件不变。

**PR 复审 Minor-2/Minor-3 收敛（2026-07-25 · 纯文档）**：Minor-2——R6/§5/§9/File Structure/Task 5 的 Kconfig 描述补全「H.264 参数 + 停流看门狗」、§6 控制流的 `SPIRAM` 订正到 `.esp32p4`、§9「不做 H.264 输出」改「默认不输出、可切换」；Minor-3——D3「<10%」订正为「约 6%–13%」、Q48 删除「逼近 USB-HS 带宽」错误表述（与 Q41/Q4 矛盾）、单摄原因改为单 sensor/单 CSI/单 OTG。纯文档、固件不变。

---

## Task 13: 停流功耗修复（可选特性，宏化方案 B · 2026-07-21 新增，设计见 spec §13）

> **已实施（2026-07-24）**。经 brainstorming + grilling 定稿、多轮非只读子代理复审收敛（详见 spec §13 与 §11 记录、本 Task 末尾执行结果）。选项 Kconfig `default n`（中性/对齐蓝本）、**本项目 `sdkconfig.defaults` 覆盖为默认开**（真机实测停流回落 ~60mA）；宏关时代码被预处理器剔除、零副作用。Cloud 双态构建通过。
> **验证约定**：本仓库无单元测试框架、Cloud 无硬件，故验证 = 宏关/宏开两配置 `idf.py build` + 真机（用户）；不写自动化测试（同 CI「仅构建」）。

**Files:**
- Modify: `main/Kconfig.projbuild`（menu 顶层、`if FORMAT_MJPEG_CAM1` 块的 `endif` 之后追加两项——本特性与编码格式无关，见执行结果 M1）
- Modify: `main/main_uvc.c`（`#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF` 包裹：状态变量 + `s_stream_lock` mutex + `stream_off_locked`/看门狗任务 + `fb_get`/`stop_cb`/`start_cb` 加锁插桩 + `app_main` 建锁与任务；顶部 include `freertos/semphr.h`）
- Docs（随 PR 末次提交、遵循 doc-last 约定）：`spec §13` 标实施完成/真机待验证、本 plan 追加执行结果、`AGENTS.md` 的「选择运行哪个 demo」按需补一句可选 `EXAMPLE_UVC_IDLE_STREAMOFF` 开关

- [x] **Step 1：`main/Kconfig.projbuild` 追加两项**（menu 顶层：`if FORMAT_MJPEG_CAM1` 的 `endif` 之后、`endmenu` 之前——本特性与编码格式无关、不随 MJPEG 门控隐藏，见执行结果 M1）

```kconfig
    config EXAMPLE_UVC_IDLE_STREAMOFF
        bool "Stop capture when host stops streaming (idle power saving)"
        default n
        help
            启用后，看门狗在 host 停止取流达 EXAMPLE_UVC_IDLE_STREAMOFF_MS 后
            STREAMOFF 采集流水线，预期使空闲电流回落到未拉流基线；host 重新拉流
            （PROBE→COMMIT）时重启采集。默认关（对齐官方蓝本）。
            注：功耗回落与 PROBE→COMMIT 恢复已真机实测达成；suspend→resume / isoc 等边界仍待验证（Cloud 仅构建）。
            本特性与编码格式无关（MJPEG/H264 均适用），故置于 menu 顶层、
            不随 FORMAT_MJPEG_CAM1 门控隐藏。

    config EXAMPLE_UVC_IDLE_STREAMOFF_MS
        int "Idle timeout before STREAMOFF (ms)"
        depends on EXAMPLE_UVC_IDLE_STREAMOFF
        default 1000
        range 200 60000
        help
            host 连续这么久未取走一帧 → 判定已停流 → STREAMOFF。
            range 下限 200ms 为硬约束（等于看门狗轮询周期）；生产建议取值显著大于帧间隔（默认 1000ms 安全）。
```

- [x] **Step 2：`main/main_uvc.c` 加状态变量 + include**（状态变量紧接 `static volatile uint32_t s_uvc_frame_count;` 之后；并在文件顶部 include 区追加 `#include "freertos/semphr.h"`）

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
// 停流看门狗状态（跨任务，volatile 保证可见性）：
//   s_fb_seq        —— 每成功取一帧 +1（单写者 fb_get、多读者看门狗）
//   s_stream_paused —— 初值 true=开机未在流；仅在 s_stream_lock 临界区内改
//   s_stream_gen    —— 每次成功 STREAMON +1；看门狗据此识别「流已被 commit 重启」（防交错误停）
//   s_stream_lock   —— 互斥锁，串行化 start/stop，防看门狗与 TinyUSB 的 commit_cb/suspend_cb 并发交错
static volatile bool s_stream_paused = true;
static volatile uint32_t s_fb_seq;
static volatile uint32_t s_stream_gen;
static SemaphoreHandle_t s_stream_lock;
#endif
```

- [x] **Step 3：加「锁内 STREAMOFF 助手」+ 看门狗任务**（放在 `video_start_cb` 之后、`video_stop_cb` 之前——**不可放 `video_fb_return_cb` 之后**：当前基线 `video_stop_cb` 在约 L407、位于 `video_fb_get_cb`/`video_fb_return_cb` 之前，而 Step 4 的 `video_stop_cb` 要调 `stream_off_locked`，C 语言须先定义/声明后调用；`stream_off_locked` 在前、`stream_watchdog_task` 在后，二者都置于 `video_stop_cb` 之前，即可同时满足 `video_stop_cb`(调 helper)/`stream_watchdog_task`(调 helper)/`app_main`(建 watchdog) 三处引用顺序，无需前向声明）

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
// 【调用者须已持 s_stream_lock】置 paused + STREAMOFF 采集/编码流水线；看门狗与 video_stop_cb 共用唯一出口
static void stream_off_locked(uvc_t *uvc)
{
    int type;
    if (!s_stream_paused) {
        ESP_LOGI(TAG, "UVC stream paused (STREAMOFF)");  // 仅真翻转打一条（看门狗/commit/suspend 共用、压噪）
    }
    s_stream_paused = true;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(uvc->cap_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(uvc->m2m_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(uvc->m2m_fd, VIDIOC_STREAMOFF, &type);
}

// 电平式循环：暂停期保鲜基线 / 帧推进或流(重)启刷新停滞时钟 / 停滞超阈值 → 锁内 generation double-check 后 STREAMOFF
static void stream_watchdog_task(void *arg)
{
    uvc_t *uvc = (uvc_t *)arg;
    uint32_t last_seq = 0, last_gen = 0;
    TickType_t last_tick = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF_MS);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        TickType_t now = xTaskGetTickCount();
        if (s_stream_paused) {                          // 暂停期持续保鲜、停滞时钟归零
            last_seq = s_fb_seq; last_gen = s_stream_gen; last_tick = now;
        } else if (s_fb_seq != last_seq || s_stream_gen != last_gen) {  // 帧推进 或 流(重)启(gen 变) → 刷新停滞时钟+基线
            last_seq = s_fb_seq; last_gen = s_stream_gen; last_tick = now;  // gen 分支保证 STREAMON 后即使零帧也重置计时，否则 last_gen 永久落后、double-check 恒跳过、看门狗永不停流（Major）
        } else if ((now - last_tick) >= timeout) {      // 停滞超阈值 → 锁内二次确认后 STREAMOFF
            xSemaphoreTake(s_stream_lock, portMAX_DELAY);
            // double-check：未暂停 + 仍停滞 + 流未被 commit 重启（gen 未变）才停，避免与 start_cb 交错误停刚恢复的流
            if (!s_stream_paused && s_fb_seq == last_seq && s_stream_gen == last_gen) {
                stream_off_locked(uvc);
            }
            xSemaphoreGive(s_stream_lock);
            last_tick = now;                            // fire 后刷新基线（防同 200ms 窗口内重复触发，Major#1）
        }
    }
}
#endif
```

- [x] **Step 4：改写 `video_stop_cb` 为「持锁调 stream_off_locked」**（宏开时用 `s_stream_lock` + `stream_off_locked` 取代原 3 路 STREAMOFF、与看门狗/start 串行；宏关时保持蓝本原样）。整个函数改为：

```c
static void video_stop_cb(void *cb_ctx)
{
    uvc_t *uvc = (uvc_t *)cb_ctx;

    ESP_LOGD(TAG, "UVC stop");

#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    xSemaphoreTake(s_stream_lock, portMAX_DELAY);
    stream_off_locked(uvc);   // 置 paused + 3 路 STREAMOFF（与看门狗共用唯一出口、锁内串行）
    xSemaphoreGive(s_stream_lock);
#else
    int type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(uvc->cap_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(uvc->m2m_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(uvc->m2m_fd, VIDIOC_STREAMOFF, &type);
#endif
}
```

- [x] **Step 5：`video_start_cb` 加锁 + gen++ + 清 paused**（整个 start 与看门狗/stop 串行、防 STREAMON 与并发 STREAMOFF 交错；**函数内其余失败点均 `ESP_ERROR_CHECK`/`assert`=abort 不返回，故仅下述早退与结尾两条 `return` 需配对 give、无锁泄漏**），三处插桩：

  (a) 函数开头 `ESP_LOGD(TAG, "UVC start");` 之后 take 锁：

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    xSemaphoreTake(s_stream_lock, portMAX_DELAY);   // 与 stop/看门狗串行；覆盖整个流配置+STREAMON
#endif
```

  (b) 唯一早退分支 `if (!capture_fmt) { ESP_LOGI(...); return ESP_ERR_NOT_SUPPORTED; }` 的 `return` 之前 give 锁（防早退漏放锁）：

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
        xSemaphoreGive(s_stream_lock);
#endif
```

  (c) 三路 STREAMON 最后一句 `ESP_ERROR_CHECK(ioctl(uvc->cap_fd, VIDIOC_STREAMON, &type));` 之后、`return ESP_OK;` 之前：

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    if (s_stream_paused) {
        ESP_LOGI(TAG, "UVC stream resumed (STREAMON)");  // 恢复翻转日志（仅真翻转时）
    }
    s_stream_gen++;           // 标记「流已（重）启动」，供看门狗 double-check 识别、避免误停刚恢复的流
    s_stream_paused = false;  // 仅此一处清 paused
    xSemaphoreGive(s_stream_lock);
#endif
```

- [x] **Step 6：`video_fb_get_cb` 暂停守卫 + 帧序号自增**
  (a) 函数开头 `ESP_LOGD(TAG, "UVC get");` 之后插入：

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    if (s_stream_paused) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 暂停期不 DQBUF（防 STREAMOFF 后 ESP_ERROR_CHECK panic）+ 降噪
        return NULL;
    }
#endif
```
  (b) 成功路径 `s_uvc_frame_count++;` 之后插入：

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    s_fb_seq++;
#endif
```

- [x] **Step 7：`app_main` 创建 mutex（`init_uvc` 前）+ 看门狗任务（monitor 后）**

  ⚠️ **mutex 必须在 `init_uvc(uvc)` 之前创建**：`init_uvc`→`uvc_device_init()` 会启动 TinyUSB/UVC 回调任务（`managed_components/espressif__usb_device_uvc/usb_device_uvc.c` L373/376），若 HUSB 已连接，`commit_cb`(L298 `stop_cb`/L307 `start_cb`)、`tud_suspend_cb`(L108 `stop_cb`) 可能在 `app_main` 后续代码执行前就触发 `video_start_cb`/`video_stop_cb`→`xSemaphoreTake(s_stream_lock)`；若锁尚未创建（NULL），`xSemaphoreTake(NULL)` 会 assert 失败/崩溃。看门狗任务无此约束（`s_stream_paused` 初值 true、开机走保鲜分支不误触发），仍放 monitor 之后。

  (a) `ESP_ERROR_CHECK(init_codec_video(uvc));` 之后、`ESP_ERROR_CHECK(init_uvc(uvc));` **之前**插入建锁：

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    s_stream_lock = xSemaphoreCreateMutex();
    assert(s_stream_lock);  // 须早于 uvc_device_init 启动的 TinyUSB/UVC 回调任务，防回调撞 NULL 锁
#endif
```

  (b) `xTaskCreate(monitor_task, "monitor", 4096, NULL, 1, NULL);` 之后插入建看门狗任务（**`xTaskCreate` 不放进 `assert()`**——关闭断言时 `assert(expr)` 展开为 `((void)0)`、整个有副作用的调用会被预处理掉、任务静默不建；对齐 monitor task 的裸调用风格）：

```c
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    BaseType_t wd_ok = xTaskCreate(stream_watchdog_task, "uvc_wd", 4096, uvc, 1, NULL);
    assert(wd_ok == pdPASS);  // 建失败即 abort，不静默禁用特性
#endif
```

- [x] **Step 8：构建验证（宏关，默认）**

> ⚠️ 历史步骤：本步写于「看门狗默认关」时期；2026-07-24 起 `sdkconfig.defaults` 已把默认翻转为**宏开**（见执行结果「默认由关改为开」），故此处「宏关=默认」及 `rm sdkconfig` 后 `grep '… is not set'` 仅反映翻转前流程；**当前默认构建即宏开态**、宏关须手动关（见 2026-07-25 执行结果）。

Run: `cd /workspace && rm -f sdkconfig && source /opt/esp/idf/export.sh >/dev/null 2>&1 && idf.py build && grep -q '^# CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF is not set$' sdkconfig && echo "宏关确认 OK" && idf.py size`
Expected: 构建成功；**新代码全在 `#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF` 内、宏关被预处理器剔除**，故体积与未改动前基本一致（无「修改前精确基线」时以此为据）；`grep` 命中 `# CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF is not set`、输出「宏关确认 OK」；**记下 `idf.py size` 的 Total image 体积**（宏关态数值，供 Step 11b 与宏开对比）。

- [x] **Step 9：构建验证（宏开）**

Run:
```bash
cd /workspace && rm -f sdkconfig && idf.py reconfigure \
  && printf '\nCONFIG_EXAMPLE_UVC_IDLE_STREAMOFF=y\n' >> sdkconfig \
  && idf.py build \
  && grep -q '^CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF=y$' sdkconfig \
  && grep -q '^CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF_MS=1000$' sdkconfig \
  && echo "宏开确认 OK" \
  && idf.py size
status=$?; rm -f sdkconfig; exit $status   # 记宏开体积后复位默认关；按验证链真实结果退出（防末尾 rm 掩盖 build/grep 失败）
```
注：ESP-IDF 的 Kconfig 项**不能**用 `idf.py -DCONFIG_x=y` 覆盖（`-D` 只设 CMake cache 变量、不进 Kconfig）；须经 `sdkconfig`/`sdkconfig.defaults`/`-DSDKCONFIG_DEFAULTS`。故先 `reconfigure` 生成 `sdkconfig`、再追加该项并 `grep` 断言其确为 `=y`。
Expected: 构建成功、无新增警告（`stream_off_locked`/`stream_watchdog_task`/`s_stream_paused`/`s_fb_seq`/`s_stream_gen`/`s_stream_lock` 均编入）、**两条 `grep`（`…STREAMOFF=y` + `…STREAMOFF_MS=1000`）均命中**、输出「宏开确认 OK」；**记下 `idf.py size` 的 Total image 体积**（宏开态，供 Step 11b 对比、应略大于宏关基线）。验证链结束后 `rm -f sdkconfig` 复位；`build/` 此刻仍为宏开产物，由下一步 Step 9b 复位。

- [x] **Step 9b：复位宏关最终态（确保最终产物 = 提交默认）**

> ⚠️ 历史步骤：同 Step 8，默认已于 2026-07-24 翻转为**宏开**；本步「复位宏关 = 提交默认」仅适用翻转前。**当前「提交默认」为宏开**，`rm sdkconfig` 后默认构建即得宏开产物。

Run: `cd /workspace && rm -f sdkconfig && idf.py build && grep -q '^# CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF is not set$' sdkconfig && echo "复位宏关 OK"`
Expected: 构建成功；`sdkconfig`（默认关）与 `build/` 产物均回到宏关态，与 Step 10 将提交的源码默认一致（避免最终 `build/` 停在 Step 9 的宏开产物）。

- [x] **Step 10：commit**

```bash
git -C /workspace add main/Kconfig.projbuild main/main_uvc.c
git -C /workspace commit -F - <<'EOF'
feat(main_uvc): :sparkles: 可选停流功耗看门狗（EXAMPLE_UVC_IDLE_STREAMOFF，默认开）

- 停流后按帧序号停滞超时（默认 1000ms）STREAMOFF 采集流水线、空闲功耗实测回落 ~60mA，host 重拉流（PROBE→COMMIT）预期自动恢复
- 电平式看门狗 + s_stream_lock 互斥 + s_stream_gen generation：start/stop/看门狗串行、锁内 double-check 防误停刚恢复的流
- paused 下沉到 video_stop_cb 入口（挡 STREAMOFF 后新进入的 DQBUF、不覆盖 in-flight）
- Kconfig 选项 default n（中性），sdkconfig.defaults 覆盖 =y 默认开；宏关零运行时影响；设计随 C7 文档化（spec §13，doc-last）
EOF
```

- [x] **Step 11：文档回填（doc-last 单独提交）**

  - **11a spec**：`§13「验证」`节 Cloud 项标为已完成（宏关/宏开 build 通过 + `grep` 断言），真机项保留「待验证」；顶部状态由「Task 13 待实现」更新为「Task 13 已实施、本项目默认开、真机待验证」；`spec §11` 追加一条聚焦实施结果的记录，不写 review 轮次。
  - **11b 本 plan**：勾选 Task 13 各步（含 9b）并在「执行结果」追加 Task 13 段，记录宏关/宏开体积、问题与真机待验证项。Architecture/File Structure/Task 3/AGENTS 已在本轮提前同步为当前 GPIO0 + monitor + bulk 最终态；Task 6 与既有执行结果继续保留“初始导入 416 行、逐字复制”的历史语义。
  - **11c `AGENTS.md`**（「选择运行哪个 demo」的 uvc 条目）：
    - ① 补一句可选开关（接在“JPEG 质量可在 menuconfig 调整”那句之后）：『另可在 `idf.py menuconfig` 的 "Example Configuration" 可选 `EXAMPLE_UVC_IDLE_STREAMOFF`（本项目 `sdkconfig.defaults` 默认开、Kconfig 选项 default n）——host 停流超时后 STREAMOFF 采集流水线、空闲电流回落 ~60mA（真机实测）；默认 bulk 下 host 重开（PROBE→COMMIT）重启采集（suspend→resume 或 isoc 仅切 alt-setting 不重发 COMMIT 则不保证恢复，详见 spec §13）。』
    - ② 在当前「基于蓝本 + GPIO0 上电 + monitor」描述后补入“可选停流看门狗”，不再重复修正已完成的蓝本偏离说明。
  - **11d 提交（doc-last，单独一个提交）**：

```bash
git -C /workspace add docs/superpowers/specs/2026-07-17-uvc-camera-design.md docs/superpowers/plans/2026-07-17-uvc-camera.md AGENTS.md
git -C /workspace commit -F - <<'EOF'
docs(docs/superpowers): :memo: 回填 uvc 停流功耗看门狗（Task 13）实施结果

- spec §13 标实施完成、真机待验证
- plan Task 13 勾选步骤 + 追加执行结果
- AGENTS.md demo 说明补可选 EXAMPLE_UVC_IDLE_STREAMOFF 开关、更正 main_uvc.c 已适配偏离蓝本
EOF
```

- [x] **Step 12：最终核验与交付**

  - `git -C /workspace status --short` 确认无误**暂存**生成物：`dependencies.lock` 允许以未跟踪（`??`）存在但**不得 `git add`**；`sdkconfig` / `managed_components/` 已 gitignore；`.ref_tmp/`、`.cursor/skills/grilling/` 已在 `.git/info/exclude`。应作为提交项的仅 `main/*`（C6 看门狗代码）与三个文档（C7 docs）。
  - `git -C /workspace log --oneline -3` 确认顺序：代码提交（Step 10）在前、文档回填提交（Step 11d）在后（doc-last）。
  - push 当前分支：`git -C /workspace push -u origin HEAD`（网络失败按指数退避 4s/8s/16s/32s 重试）。
  - 用 ManagePullRequest 更新 PR #4：提交表反映 **7 个提交**（C6=停流看门狗、C7=文档、doc-last、Task 13 文档未单列 C8）；并把正文过时表述更新为定稿口径——「停流功耗修复方案 A/B/C/D 待定」→「已实施宏化方案 B（`EXAMPLE_UVC_IDLE_STREAMOFF`，本项目默认开、含 mutex+generation 串行化）」、「host 停流不产生任何 USB 事件」→「host 停流无标准显式停流信号」。

**Task 13 完成标准**：**Cloud（本 plan 完成判据）**——宏关/宏开两配置 `idf.py build` 均通过、宏关无新增运行时代码路径（新增码全在 `#if` 内）、双配置 `grep` 断言命中（宏开含 `MS=1000`）。**真机（由用户验证、不阻塞 Cloud 完成，对齐「执行前必读」的「Cloud 只能 build 验证」条）**——关 VLC / 断 HUSB 后约 1.0–1.2s 回落 ~60mA（实测已达成）、重开经 PROBE→COMMIT 恢复出图、持续暂停期本特性错误日志（`Failed to capture`）≤1 行/秒、暂停/恢复翻转各一条 `ESP_LOGI` 可见。
