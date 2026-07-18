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
- `sdkconfig.defaults.esp32p4`（P4 target 专属）声明 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` 与 `CONFIG_ESP32P4_REV_MIN_0=y`，用于适配 revision <3.0 的样片（实测板为 v1.3，最低支持修订 Rev v0.0）；这只影响真实烧录时的芯片修订校验，不影响 Cloud 内的纯构建。**注意 ESP32-P4 的 rev <3.0 与 rev ≥3.0 互斥**：本配置面向 <3.0 工程样片，若改用 rev ≥3.0 量产芯片，需相应切换 revision 配置（去掉 `SELECTS_REV_LESS_V3`）。
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

源文件采用 `main_<demo>.c` 命名约定（历史遗留的空白入口 `main.c` 除外）。当前可选 demo（按加入顺序，最新的在最后）：

- `main.c`：空 `app_main` 占位（最小基线）。
- `main_hello_world.c`：打印芯片信息与 "Hello world" 的基础示例。
- `main_blink_example.c`：点亮板载 RGB。源自 ESP-IDF 官方 Blink 示例，配置为可寻址 `led_strip`（GPIO51、RMT backend、周期 1000ms），依赖 `led_strip`（managed component，`main/idf_component.yml` 声明 `^3.0.3`，乐鑫官方组件省略 `espressif/` 前缀；不提交 `dependencies.lock`、不锁定解析版本）。可用 `idf.py menuconfig` 的 "Example Configuration" 调整 LED 类型 / backend / GPIO / 周期；默认配置见根 `sdkconfig.defaults`。注意：`sdkconfig.defaults*` 只对 `sdkconfig` 中尚不存在的符号生效，改了默认后若不生效需 `rm -f sdkconfig` 重新构建。（RMT backend 的详细说明见 `docs/superpowers` 下的 spec「补充解释：RMT backend 是什么？」。）
- `main_uvc.c`（**默认激活**）：把开发板变成 USB 摄像头（UVC Device）。**OV5647**（树莓派原生线序、**同向 FPC 直插**，反向接会电源反接、有烧毁风险）经 MIPI-CSI 采集 RAW10 1920×1080，走 P4 **ISP** + **硬件 JPEG** 压成 **MJPEG**，默认通过 **USB UVC bulk（HUSB 高速口，480Mbps）** 输出；初始实现曾用 isoc，真机发现约 65Mbps 单微帧带宽瓶颈后改为 bulk。macOS 用 QuickTime / Photo Booth / OBS 打开（`ffmpeg` avfoundation 抓 MJPEG 有 bug、勿用）。代码基于乐鑫官方 `esp_video/examples/uvc`，并完成本板适配：`app_main()` 拉高 GPIO0 给 OV5647 模块上电，monitor 每 5 秒打印 fps/CPU/内存/逐任务信息；板级复用本地 `components/example_video_common`（customized、SCCB `SCL=8/SDA=7`、`XCLK_PIN=-1` 靠模块自带 25MHz 晶振）。依赖 `esp_video ^2.3.0` / `usb_device_uvc ^1.3.1` / `esp_cam_sensor ^2.3.0` + 本地 `example_video_common`。默认配置见 `sdkconfig.defaults`（1920×1080@30 目标、device 侧实测 ~15–16fps、bulk、monitor、FREERTOS_HZ/运行时统计/分区表等应用/系统项）与 `sdkconfig.defaults.esp32p4`（16MB flash、SPIRAM 等 N16R32 模组/P4 target 硬件属性）；JPEG 质量（及切 H264 时的 H264 码率/QP 等）可在 `idf.py menuconfig` 的 "Example Configuration" 调整——Kconfig 已备齐 MJPEG/H264 两套参数对齐官方蓝本，默认 MJPEG，切 H264 见 `sdkconfig.defaults`「编码格式档」（2 选 1 整组注释、同分辨率档一键切换，MJPEG/H264 双态构建均通过）；另有 `EXAMPLE_UVC_IDLE_STREAMOFF`（本项目 `sdkconfig.defaults` **默认开**、Kconfig 选项 default n）——host 停止取流超时（默认 1000ms）即 STREAMOFF 采集流水线、空闲电流实测回落 ~60mA，host 重开（PROBE→COMMIT）自动恢复（suspend→resume 或 isoc 仅切 alt-setting 不重发 COMMIT 则不保证恢复，详见 spec §13）。Cloud 仅能构建，帧率/画质/功耗需真机验证。

### 测试 / lint

本仓库没有单元测试或 lint 配置。CI 仅构建固件，并输出 `idf.py size` 与 `esptool image-info`（CI 已兼容 esptool 4.x/5.x 两种子命令）。
