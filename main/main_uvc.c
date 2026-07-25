/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb_device_uvc.h"
#include "uvc_frame_config.h"
#include "example_video_common.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"

#if CONFIG_FORMAT_MJPEG_CAM1
#define ENCODE_DEV_PATH     ESP_VIDEO_JPEG_DEVICE_NAME
#define UVC_OUTPUT_FORMAT   V4L2_PIX_FMT_JPEG
#elif CONFIG_FORMAT_H264_CAM1
#if CONFIG_EXAMPLE_H264_MAX_QP <= CONFIG_EXAMPLE_H264_MIN_QP
#error "CONFIG_EXAMPLE_H264_MAX_QP should larger than CONFIG_EXAMPLE_H264_MIN_QP"
#endif

#define ENCODE_DEV_PATH     ESP_VIDEO_H264_DEVICE_NAME
#define UVC_OUTPUT_FORMAT   V4L2_PIX_FMT_H264
#endif

#define BUFFER_COUNT        2

typedef struct uvc {
    int cap_fd;
    uint32_t format;
    uint8_t *cap_buffer[BUFFER_COUNT];

    int m2m_fd;
    uint8_t *m2m_cap_buffer;

    uvc_fb_t fb;
} uvc_t;

// 摄像头电源使能脚：本板 J2 Pin5=IO0(GPIO0)=CAM_IO0(Power-Enable, active-high)
#define CAM_PWR_EN_GPIO 0

static const char *TAG = "example";

// 性能监视：UVC 每提供一帧 s_uvc_frame_count++，monitor_task 每隔 MONITOR_PERIOD_MS 汇总 fps + CPU + 内存 + 逐任务明细
// CPU 占比算法参考 ESP-IDF 官方示例 system/freertos/real_time_stats（两次采样差分、分母除以核数）
#define MONITOR_PERIOD_MS   5000
static volatile uint32_t s_uvc_frame_count;

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

static void monitor_task(void *arg)
{
    // 任务快照缓冲上限 24：任务数超此则 uxTaskGetSystemState 返回 0、该周期逐任务 CPU 明细降级为不显示（本例任务数远小于 24）
    static TaskStatus_t prev[24];
    UBaseType_t prev_num = 0;
    uint32_t prev_total = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_PERIOD_MS));

        // fps 换算为每秒帧率（采样周期内累计帧数 ÷ 周期秒数）
        uint32_t fps = s_uvc_frame_count * 1000 / MONITOR_PERIOD_MS;
        s_uvc_frame_count = 0;

        size_t int_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t int_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

        // 两次采样各任务累计运行时间，算过去 5 秒的 runtime 增量（按 handle 匹配上次采样）
        TaskStatus_t cur[24];
        uint32_t cur_total = 0;
        UBaseType_t cur_num = uxTaskGetSystemState(cur, 24, &cur_total);
        // 分母 = 单核 runtime 增量 × 核数（对齐官方 real_time_stats；双核下各任务占比合计约 100%）
        uint32_t denom = (prev_num && cur_num && cur_total > prev_total)
                             ? ((cur_total - prev_total) * CONFIG_FREERTOS_NUMBER_OF_CORES) : 0;

        uint32_t rt_delta[24] = {0};
        uint32_t idle_delta = 0;
        for (UBaseType_t i = 0; i < cur_num; i++) {
            for (UBaseType_t j = 0; j < prev_num; j++) {
                if (cur[i].xHandle == prev[j].xHandle) {
                    rt_delta[i] = cur[i].ulRunTimeCounter - prev[j].ulRunTimeCounter;
                    break;
                }
            }
            if (strncmp(cur[i].pcTaskName, "IDLE", 4) == 0) {
                idle_delta += rt_delta[i];
            }
        }
        int cpu = denom ? (100 - (int)((idle_delta * 100) / denom)) : -1;

        // 空行分隔每次采样输出，避免多组连在一起
        ESP_LOGI(TAG, " ");

        // 总览行
        if (cpu >= 0) {
            ESP_LOGI(TAG, "perf | fps=%u | CPU=%d%% | INT free=%uKB(min %uKB) | PSRAM free=%uKB",
                     (unsigned)fps, cpu, (unsigned)(int_free / 1024), (unsigned)(int_min / 1024), (unsigned)(psram_free / 1024));
        } else {
            ESP_LOGI(TAG, "perf | fps=%u | INT free=%uKB(min %uKB) | PSRAM free=%uKB",
                     (unsigned)fps, (unsigned)(int_free / 1024), (unsigned)(int_min / 1024), (unsigned)(psram_free / 1024));
        }

        // 逐任务明细（TaskStatus_t 全部字段；列参考 ESP-IDF vTaskList：名/状态/优先级/HWM/编号/核，另加 CPU%/句柄/栈基址）
        // St：Run/Rdy/Blk/Sus/Del；Pri(c/b)=当前/基础优先级；CPU%=过去 5 秒占比；StkMin=栈剩余最低水位(字节,越小越危险)
        if (denom) {
            ESP_LOGI(TAG, "  ===========================================================================");
            ESP_LOGI(TAG, "  %-15s %-4s %-4s %-3s %-8s %-4s %-9s %-10s %-10s",
                     "TaskName", "Num", "Core", "St", "Pri(c/b)", "CPU%", "StkMin(B)", "Handle", "StackBase");
            for (UBaseType_t i = 0; i < cur_num; i++) {
                const char *st;
                switch (cur[i].eCurrentState) {
                    case eRunning:   st = "Run"; break;
                    case eReady:     st = "Rdy"; break;
                    case eBlocked:   st = "Blk"; break;
                    case eSuspended: st = "Sus"; break;
                    case eDeleted:   st = "Del"; break;
                    default:         st = "Inv"; break;
                }
                char core[3];
#if configTASKLIST_INCLUDE_COREID
                core[0] = (cur[i].xCoreID == tskNO_AFFINITY) ? '*' : (char)('0' + cur[i].xCoreID);
#else
                core[0] = '?';
#endif
                core[1] = '\0';
                ESP_LOGI(TAG, "  %-15s %-4u %-4s %-3s %2u/%-5u %3d%% %-9u %-10p %p",
                         cur[i].pcTaskName,
                         (unsigned)cur[i].xTaskNumber,
                         core, st,
                         (unsigned)cur[i].uxCurrentPriority, (unsigned)cur[i].uxBasePriority,
                         (int)((rt_delta[i] * 100) / denom),
                         (unsigned)cur[i].usStackHighWaterMark,
                         (void *)cur[i].xHandle,
                         (void *)cur[i].pxStackBase);
            }
        }

        if (cur_num) {
            memcpy(prev, cur, sizeof(TaskStatus_t) * cur_num);
            prev_num = cur_num;
            prev_total = cur_total;
        }
    }
}

static void print_video_device_info(const struct v4l2_capability *capability)
{
    ESP_LOGI(TAG, "version: %d.%d.%d", (uint16_t)(capability->version >> 16),
             (uint8_t)(capability->version >> 8),
             (uint8_t)capability->version);
    ESP_LOGI(TAG, "driver:  %s", capability->driver);
    ESP_LOGI(TAG, "card:    %s", capability->card);
    ESP_LOGI(TAG, "bus:     %s", capability->bus_info);
    ESP_LOGI(TAG, "capabilities:");
    if (capability->capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        ESP_LOGI(TAG, "\tVIDEO_CAPTURE");
    }
    if (capability->capabilities & V4L2_CAP_READWRITE) {
        ESP_LOGI(TAG, "\tREADWRITE");
    }
    if (capability->capabilities & V4L2_CAP_ASYNCIO) {
        ESP_LOGI(TAG, "\tASYNCIO");
    }
    if (capability->capabilities & V4L2_CAP_STREAMING) {
        ESP_LOGI(TAG, "\tSTREAMING");
    }
    if (capability->capabilities & V4L2_CAP_META_OUTPUT) {
        ESP_LOGI(TAG, "\tMETA_OUTPUT");
    }
    if (capability->capabilities & V4L2_CAP_DEVICE_CAPS) {
        ESP_LOGI(TAG, "device capabilities:");
        if (capability->device_caps & V4L2_CAP_VIDEO_CAPTURE) {
            ESP_LOGI(TAG, "\tVIDEO_CAPTURE");
        }
        if (capability->device_caps & V4L2_CAP_READWRITE) {
            ESP_LOGI(TAG, "\tREADWRITE");
        }
        if (capability->device_caps & V4L2_CAP_ASYNCIO) {
            ESP_LOGI(TAG, "\tASYNCIO");
        }
        if (capability->device_caps & V4L2_CAP_STREAMING) {
            ESP_LOGI(TAG, "\tSTREAMING");
        }
        if (capability->device_caps & V4L2_CAP_META_OUTPUT) {
            ESP_LOGI(TAG, "\tMETA_OUTPUT");
        }
    }
}

static esp_err_t init_capture_video(uvc_t *uvc)
{
    int fd;
    struct v4l2_capability capability;

    fd = open(EXAMPLE_CAM_DEV_PATH, O_RDONLY);
    assert(fd >= 0);

    ESP_ERROR_CHECK(ioctl(fd, VIDIOC_QUERYCAP, &capability));
    print_video_device_info(&capability);

    uvc->cap_fd = fd;

    return 0;
}

static esp_err_t init_codec_video(uvc_t *uvc)
{
    int fd;
    const char *devpath = ENCODE_DEV_PATH;
    struct v4l2_capability capability;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

    fd = open(devpath, O_RDONLY);
    assert(fd >= 0);

    ESP_ERROR_CHECK(ioctl(fd, VIDIOC_QUERYCAP, &capability));
    print_video_device_info(&capability);

#if CONFIG_FORMAT_MJPEG_CAM1
    controls.ctrl_class = V4L2_CID_JPEG_CLASS;
    controls.count      = 1;
    controls.controls   = control;
    control[0].id       = V4L2_CID_JPEG_COMPRESSION_QUALITY;
    control[0].value    = CONFIG_EXAMPLE_JPEG_COMPRESSION_QUALITY;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "failed to set JPEG compression quality");
    }
#elif CONFIG_FORMAT_H264_CAM1
    controls.ctrl_class = V4L2_CID_CODEC_CLASS;
    controls.count      = 1;
    controls.controls   = control;
    control[0].id       = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
    control[0].value    = CONFIG_EXAMPLE_H264_I_PERIOD;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "failed to set H.264 intra frame period");
    }

    controls.ctrl_class = V4L2_CID_CODEC_CLASS;
    controls.count      = 1;
    controls.controls   = control;
    control[0].id       = V4L2_CID_MPEG_VIDEO_BITRATE;
    control[0].value    = CONFIG_EXAMPLE_H264_BITRATE;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "failed to set H.264 bitrate");
    }

    controls.ctrl_class = V4L2_CID_CODEC_CLASS;
    controls.count      = 1;
    controls.controls   = control;
    control[0].id       = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
    control[0].value    = CONFIG_EXAMPLE_H264_MIN_QP;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "failed to set H.264 minimum quality");
    }

    controls.ctrl_class = V4L2_CID_CODEC_CLASS;
    controls.count      = 1;
    controls.controls   = control;
    control[0].id       = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
    control[0].value    = CONFIG_EXAMPLE_H264_MAX_QP;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "failed to set H.264 maximum quality");
    }
#endif

    uvc->format = UVC_OUTPUT_FORMAT;
    uvc->m2m_fd = fd;

    return 0;
}

static esp_err_t video_start_cb(uvc_format_t uvc_format, int width, int height, int rate, void *cb_ctx)
{
    int type;
    struct v4l2_buffer buf;
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    uvc_t *uvc = (uvc_t *)cb_ctx;
    uint32_t capture_fmt = 0;

    ESP_LOGD(TAG, "UVC start");

#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    xSemaphoreTake(s_stream_lock, portMAX_DELAY);   // 与 stop/看门狗串行；覆盖整个流配置+STREAMON
#endif

    if (uvc->format == V4L2_PIX_FMT_JPEG) {
        int fmt_index = 0;
        const uint32_t jpeg_input_formats[] = {
            V4L2_PIX_FMT_RGB565,
            V4L2_PIX_FMT_UYVY,
            V4L2_PIX_FMT_RGB24,
            V4L2_PIX_FMT_GREY
        };
        int jpeg_input_formats_num = sizeof(jpeg_input_formats) / sizeof(jpeg_input_formats[0]);

        while (!capture_fmt) {
            struct v4l2_fmtdesc fmtdesc = {
                .index = fmt_index++,
                .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            };

            if (ioctl(uvc->cap_fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
                break;
            }

            for (int i = 0; i < jpeg_input_formats_num; i++) {
                if (jpeg_input_formats[i] == fmtdesc.pixelformat) {
                    capture_fmt = jpeg_input_formats[i];
                    break;
                }
            }
        }

        if (!capture_fmt) {
            ESP_LOGI(TAG, "The camera sensor output pixel format is not supported by JPEG");
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
            xSemaphoreGive(s_stream_lock);
#endif
            return ESP_ERR_NOT_SUPPORTED;
        }
    } else {
        capture_fmt = V4L2_PIX_FMT_YUV420;
    }

    /* Configure camera interface capture stream */

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = capture_fmt;
    ESP_ERROR_CHECK(ioctl(uvc->cap_fd, VIDIOC_S_FMT, &format));

    memset(&req, 0, sizeof(req));
    req.count  = BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_ERROR_CHECK(ioctl(uvc->cap_fd, VIDIOC_REQBUFS, &req));

    for (int i = 0; i < BUFFER_COUNT; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;
        ESP_ERROR_CHECK (ioctl(uvc->cap_fd, VIDIOC_QUERYBUF, &buf));

        uvc->cap_buffer[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                             MAP_SHARED, uvc->cap_fd, buf.m.offset);
        assert(uvc->cap_buffer[i]);

        ESP_ERROR_CHECK(ioctl(uvc->cap_fd, VIDIOC_QBUF, &buf));
    }

    /* Configure codec output stream */

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = capture_fmt;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_S_FMT, &format));

    memset(&req, 0, sizeof(req));
    req.count  = 1;
    req.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_REQBUFS, &req));

    /* Configure codec capture stream */

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = uvc->format;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_S_FMT, &format));

    memset(&req, 0, sizeof(req));
    req.count  = 1;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_REQBUFS, &req));

    memset(&buf, 0, sizeof(buf));
    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = 0;
    ESP_ERROR_CHECK (ioctl(uvc->m2m_fd, VIDIOC_QUERYBUF, &buf));

    uvc->m2m_cap_buffer = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                          MAP_SHARED, uvc->m2m_fd, buf.m.offset);
    assert(uvc->m2m_cap_buffer);

    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_QBUF, &buf));

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_STREAMON, &type));
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_STREAMON, &type));

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_ERROR_CHECK(ioctl(uvc->cap_fd, VIDIOC_STREAMON, &type));

#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    if (s_stream_paused) {
        ESP_LOGI(TAG, "UVC stream resumed (STREAMON)");  // 恢复翻转日志（仅真翻转时）
    }
    s_stream_gen++;           // 标记「流已（重）启动」，供看门狗 double-check 识别、避免误停刚恢复的流
    s_stream_paused = false;  // 仅此一处清 paused
    xSemaphoreGive(s_stream_lock);
#endif

    return ESP_OK;
}

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

static uvc_fb_t *video_fb_get_cb(void *cb_ctx)
{
    int64_t us;
    uvc_t *uvc = (uvc_t *)cb_ctx;
    struct v4l2_format format;
    struct v4l2_buffer cap_buf;
    struct v4l2_buffer m2m_out_buf;
    struct v4l2_buffer m2m_cap_buf;

    ESP_LOGD(TAG, "UVC get");

#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    if (s_stream_paused) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 暂停期不 DQBUF（防 STREAMOFF 后 ESP_ERROR_CHECK panic）+ 降噪
        return NULL;
    }
#endif

    memset(&cap_buf, 0, sizeof(cap_buf));
    cap_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cap_buf.memory = V4L2_MEMORY_MMAP;
    ESP_ERROR_CHECK(ioctl(uvc->cap_fd, VIDIOC_DQBUF, &cap_buf));

    memset(&m2m_out_buf, 0, sizeof(m2m_out_buf));
    m2m_out_buf.index  = 0;
    m2m_out_buf.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    m2m_out_buf.memory = V4L2_MEMORY_USERPTR;
    m2m_out_buf.m.userptr = (unsigned long)uvc->cap_buffer[cap_buf.index];
    m2m_out_buf.length = cap_buf.bytesused;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_QBUF, &m2m_out_buf));

    memset(&m2m_cap_buf, 0, sizeof(m2m_cap_buf));
    m2m_cap_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    m2m_cap_buf.memory = V4L2_MEMORY_MMAP;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_DQBUF, &m2m_cap_buf));

    ESP_ERROR_CHECK(ioctl(uvc->cap_fd, VIDIOC_QBUF, &cap_buf));
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_DQBUF, &m2m_out_buf));

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_G_FMT, &format));

    uvc->fb.buf = uvc->m2m_cap_buffer;
    uvc->fb.len = m2m_cap_buf.bytesused;
    uvc->fb.width = format.fmt.pix.width;
    uvc->fb.height = format.fmt.pix.height;
    uvc->fb.format = format.fmt.pix.pixelformat == V4L2_PIX_FMT_JPEG ? UVC_FORMAT_JPEG : UVC_FORMAT_H264;

    us = esp_timer_get_time();
    uvc->fb.timestamp.tv_sec = us / 1000000UL;
    uvc->fb.timestamp.tv_usec = us % 1000000UL;

    s_uvc_frame_count++;
#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    s_fb_seq++;
#endif
    return &uvc->fb;
}

static void video_fb_return_cb(uvc_fb_t *fb, void *cb_ctx)
{
    struct v4l2_buffer m2m_cap_buf;
    uvc_t *uvc = (uvc_t *)cb_ctx;

    ESP_LOGD(TAG, "UVC return");

    m2m_cap_buf.index  = 0;
    m2m_cap_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    m2m_cap_buf.memory = V4L2_MEMORY_MMAP;
    ESP_ERROR_CHECK(ioctl(uvc->m2m_fd, VIDIOC_QBUF, &m2m_cap_buf));
}

static esp_err_t init_uvc(uvc_t *uvc)
{
    int index = 0;
    uvc_device_config_t config = {
        .start_cb     = video_start_cb,
        .fb_get_cb    = video_fb_get_cb,
        .fb_return_cb = video_fb_return_cb,
        .stop_cb      = video_stop_cb,
        .cb_ctx       = (void *)uvc,
    };

    config.uvc_buffer_size = UVC_FRAMES_INFO[index][0].width * UVC_FRAMES_INFO[index][0].height;
    config.uvc_buffer = malloc(config.uvc_buffer_size);
    assert(config.uvc_buffer);

    ESP_LOGI(TAG, "Format List");
    ESP_LOGI(TAG, "\tFormat(1) = %s", uvc->format == V4L2_PIX_FMT_JPEG ? "MJPEG" : "H.264");
    ESP_LOGI(TAG, "Frame List");
    ESP_LOGI(TAG, "\tFrame(1) = %d * %d @%dfps", UVC_FRAMES_INFO[index][0].width, UVC_FRAMES_INFO[index][0].height, UVC_FRAMES_INFO[index][0].rate);

    ESP_ERROR_CHECK(uvc_device_config(index, &config));
    ESP_ERROR_CHECK(uvc_device_init());

    return ESP_OK;
}

void app_main(void)
{
    uvc_t *uvc = calloc(1, sizeof(uvc_t));
    assert(uvc);

    // 摄像头电源使能：本板 J2 为倒序树莓派线序，J2 Pin5=IO0(GPIO0)=CAM_IO0(Power-Enable, active-high)。
    // 拉高以给 OV5647 模块板载稳压器 + 25MHz 晶振上电；否则 sensor 无供电/无 XCLK，SCCB 恒 NACK、detect 失败。
    gpio_config_t cam_pwr_en = {
        .pin_bit_mask = 1ULL << CAM_PWR_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cam_pwr_en));
    ESP_ERROR_CHECK(gpio_set_level(CAM_PWR_EN_GPIO, 1));
    vTaskDelay(pdMS_TO_TICKS(50));  // 等供电稳定 + 晶振起振（datasheet 要求 XCLK≥1ms、AVDD→PWDN>5ms，取 50ms 富余）

    ESP_ERROR_CHECK(example_video_init());
    ESP_ERROR_CHECK(init_capture_video(uvc));
    ESP_ERROR_CHECK(init_codec_video(uvc));

#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    s_stream_lock = xSemaphoreCreateMutex();
    assert(s_stream_lock);  // 须早于 uvc_device_init 启动的 TinyUSB/UVC 回调任务，防回调撞 NULL 锁
#endif

    ESP_ERROR_CHECK(init_uvc(uvc));

    // 性能监视任务：每 5s 打印 fps / CPU / 内存（CPU 需 sdkconfig runtime stats）
    BaseType_t mon_ok = xTaskCreate(monitor_task, "monitor", 4096, NULL, 1, NULL);
    assert(mon_ok == pdPASS);

#if CONFIG_EXAMPLE_UVC_IDLE_STREAMOFF
    BaseType_t wd_ok = xTaskCreate(stream_watchdog_task, "uvc_wd", 4096, uvc, 1, NULL);
    assert(wd_ok == pdPASS);  // 建失败即 abort，不静默禁用特性
#endif
}
