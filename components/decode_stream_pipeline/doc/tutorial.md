# decode_stream_pipeline 组件详解教程

由浅入深讲解组件的设计思路、SDK 基础、内部实现和使用方法。

> **重构说明**：本组件前身为 `jpeg_stream_pipeline`（单文件，JPEG/H264 逻辑混住）。
> 现已重命名为 `decode_stream_pipeline` 并拆分为「核心 + JPEG/H264 两个编解码后端」，
> 配置新增 `decode_type` 指定初始编解码类型。接口用法除前缀更名（`jsp_` → `dsp_`）外保持不变。
> 架构细节见 [DESIGN.md](../DESIGN.md)。

---

## 第一章：这个组件解决什么问题？

### 1.1 应用场景

摩托车/电动车仪表盘通过 WiFi 从手机接收压缩视频流（JPEG 图片流或 H264 视频流，导航画面），解码后显示在 1920x1080 的 MIPI DSI 屏幕上。

数据流路径：

```
手机 App（导航）
   │  WiFi
   ▼
BK7259 AP 侧接收压缩码流（JPEG / H264）
   │
   ▼
VCDEC 硬件解码（压缩码流 → NV12 原始像素）
   │
   ▼
GPU 压缩 + 旋转（NV12 → 压缩 ARGB8888）
   │
   ▼
DPU 解压显示到 LCD 屏幕
```

### 1.2 旧方案的问题

旧的 `decode` 组件（`jpeg_decode_manager.c` + `gpu_compress_test.c`）采用**全串行**方式：

```
时间轴 ──────────────────────────────────────────────────────>

[  VCDEC 解码全帧 ~2000ms  ][  memcpy 拼帧  ][ GPU 压缩 ~300ms ][ DPU ]
```

问题列表：

| # | 问题 | 后果 |
|---|------|------|
| 1 | VCDEC 解完全部 strip 后才交给 GPU | GPU 在等 VCDEC 的 2000ms 里完全空闲 |
| 2 | 每条 strip 从 ring buffer memcpy 到线性大帧 | 浪费 ~3MB 内存带宽 |
| 3 | 手写 `vg_lite_blit` 循环做 GPU 压缩 | 未利用 SDK GPU 控制器的并行能力 |
| 4 | 每帧调 `lvgl_app_stop_for_casting()` | 额外 ~30ms 延迟 |
| 5 | 用旧的 `h264_decoder_api` | 不走 ctlr 架构，无法使用 bond 机制 |

### 1.3 新方案的改进

`decode_stream_pipeline` 利用 SDK 内置的 strip 级并行能力：

```
时间轴 ──────────────────────────────────────────────────────>

VCDEC: [strip0][strip1][strip2][strip3]...[strip67]
GPU:         [s0]   [s1]   [s2]   [s3]...[s67]
                                              └─ frame_display 回调
```

VCDEC 解出 strip 0 后，GPU **立即**开始处理 strip 0。当 GPU 处理 strip 0 时，VCDEC 已经在解 strip 1 了。两者流水线并行，GPU 的 300ms 完全隐藏在 VCDEC 的 2000ms 之中。

**消除了 3MB 线性帧 memcpy**，VCDEC 的 FLEXA ring buffer 直接通过硬件地址映射被 GPU 读取。

---

## 第二章：需要了解的 SDK 基础知识

在读代码之前，先理解三个 SDK 子系统。

### 2.1 VCDEC FLEXA 解码

BK7259 内置 VCDEC（Video Codec Decoder）硬件，支持 JPEG / H264 解码。

**FLEXA 模式**是一种流式输出模式：VCDEC 不一次输出整帧，而是每解出 16 行像素（一个 "strip"），就写入 ring buffer 的一个槽位，然后触发回调。

```
压缩码流 ──▶ VCDEC 硬件
                │
                ├──▶ ring buffer slot 0（16行 NV12 Y + UV）
                │        ↓ flexa_done_cb(wr_ptr=0)
                │
                ├──▶ ring buffer slot 1（16行 NV12 Y + UV）
                │        ↓ flexa_done_cb(wr_ptr=1)
                │
                ├──▶ ring buffer slot 0（复用，第3条 strip）
                │        ↓ flexa_done_cb(wr_ptr=2)
                ...
```

Ring buffer 大小计算：
```
ring_size = aligned_width * flexa_lines * 1.5 * buff_cnt
          = 1920 * 16 * 1.5 * 3
          = 138,240 字节 ≈ 135KB
```

其中 `1.5` 是 NV12 格式每像素字节数（Y 平面 1 字节 + UV 平面 0.5 字节），`buff_cnt=3` 表示 3 个槽位交替使用。

**关键 API（JPEG 为例，H264 为 `bk_h264_decode_*` 同构）：**
```c
bk_jpeg_decode_new(&handle, &config);   // 创建实例
bk_jpeg_decode_init(handle);            // 初始化硬件
bk_jpeg_decode_open(handle);            // 打开
bk_jpeg_decode_frame(handle, &input);   // 提交一帧解码（阻塞直到全帧解完）
bk_jpeg_decode_close(handle);           // 关闭
bk_jpeg_decode_deinit(handle);          // 反初始化
bk_jpeg_decode_delete(handle);          // 释放
```

### 2.2 bk_gpu_ctlr（GPU 控制器）

SDK 提供的 GPU 控制器封装了 VG-Lite 硬件加速引擎。在 FLEXA 模式下，它能**逐 strip 处理**：

```
收到 ioctl(LINES_READY, wr_ptr)
   │
   ▼
gpu_isp_line_done_handle()
   │
   ├── vg_lite_blit(strip)     ← 硬件加速 NV12→ARGB8888 + 可选旋转
   ├── vg_lite_finish()        ← 等待 GPU 完成
   ├── DMA 搬运到输出 buffer   ← 旋转时按列写入
   │
   ▼
通知 VCDEC 释放 ring buffer 槽位（反压）
   │
   ▼（全部 strip 完成后）
frame_display 回调 ── 输出帧就绪
```

**关键配置字段（`bk_gpu_ctlr_config_t`）：**
```c
.flexa          = true       // 启用 FLEXA strip 并行模式
.flexa_lines    = 16         // 每 strip 16 行
.flexa_buff_cnt = 3          // ring buffer 3 个槽
.src_buffer     = ring_buf   // FLEXA ring buffer 基地址
.rotate_degree  = 90         // 旋转角度（0 或 90）
.compress       = true       // DPU 压缩输出
.frame_display  = callback   // 帧完成回调
.malloc         = alloc_fn   // 输出帧分配器
```

### 2.3 FLEXA Bond 机制

Bond 是 SDK 提供的**自动信号路由层**，连接 VCDEC 和 GPU 的 strip 级握手信号：

```
                    FLEXA Bond
                 ┌──────────────┐
VCDEC ──strip就绪──▶│  正向信号     │──▶ GPU ioctl(LINES_READY)
                 │              │
GPU ──处理完毕────▶│  反向信号     │──▶ VCDEC ioctl(SET_RD_PTR) ── 释放 ring 槽
                 └──────────────┘
```

**正向（解码→GPU）：**
VCDEC 的 `flexa_done_cb` 被 bond 劫持，改为调用 `bk_gpu_ioctl(gpu, LINES_READY, wr_ptr)`。

**反向（GPU→解码）：**
GPU 处理完一个 strip 后调用解码器的 `SET_RD_PTR` ioctl，通知 VCDEC 可以复用该 ring 槽。

这就是为什么在创建解码器时 `flexa_done_cb = NULL`——回调由 bond 注入，无需手动设置。

**API（每种编解码各一个 bond）：**
```c
bk_flexa_mjpegd_gpu_bond_start(&bond, jpeg_handle, gpu_handle);  // JPEG
bk_flexa_h264d_gpu_bond_start(&bond, h264_handle, gpu_handle);   // H264
```

---

## 第三章：公开接口解析

### 3.1 数据类型

```c
/* 不透明 handle —— 调用者只拿到指针，看不到内部结构 */
typedef struct decode_stream_pipeline_ctx *decode_stream_pipeline_handle_t;

/* 初始编解码类型 */
typedef enum {
    DECODE_STREAM_TYPE_AUTO = 0,  /* 默认按 JPEG 初始化，按帧自动切换 */
    DECODE_STREAM_TYPE_JPEG,      /* JPEG 为主，H264 解码器惰性创建 */
    DECODE_STREAM_TYPE_H264,      /* H264 为主，JPEG 解码器惰性创建 */
} decode_stream_type_t;
```

### 3.2 三个回调

| 回调 | 必需？ | 运行上下文 | 作用 |
|------|--------|-----------|------|
| `dsp_frame_display_cb_t` | 是 | GPU 任务 | 输出帧就绪，接收方负责释放帧 |
| `dsp_malloc_cb_t` | 是 | GPU 任务 | 为 GPU 分配输出帧 buffer |
| `dsp_frame_consumed_cb_t` | 否 | decode 任务 / 生产者线程 | 通知码流已消费（或被丢弃）；回调含 `owner`（push_frame 第四参，可为 NULL） |

**内存所有权流转图：**

```
调用者分配压缩码流 ──push_frame──▶ pipeline 使用（零拷贝）
                                         │
                              解码完成 / 帧被丢弃
                                         │
                              frame_consumed_cb ──▶ 调用者释放码流

pipeline 内部 malloc_cb 分配输出帧 ──GPU填充──▶ frame_display_cb
                                                    │
                                              调用者使用（送 DPU）
                                                    │
                                              调用者释放输出帧
```

### 3.3 配置结构体

```c
decode_stream_pipeline_config_t cfg = {
    /* 初始编解码类型：决定 create() 时急切拉起哪个解码器。
       投屏源可能 JPEG/H264 混合 → AUTO；纯 MJPEG 开机动画 → JPEG */
    .decode_type = DECODE_STREAM_TYPE_AUTO,

    /* 源帧尺寸 —— 压缩图像实际分辨率 */
    .src_width  = 1920,
    .src_height = 1080,

    /* 目标尺寸 —— LCD 显示分辨率 */
    .dst_width  = 1080,
    .dst_height = 1920,

    /* GPU 旋转：手机横屏 1920x1080 → 竖屏 1080x1920 */
    .rotate_degree = 90,

    .dst_format = BK_PIXEL_FORMAT_ARGB8888,
    .compress   = true,    /* DPU 需要压缩数据 */
    .scale      = false,

    /* 填 0 使用默认值 */
    .flexa_lines    = 0,   /* 默认 16 */
    .flexa_buff_cnt = 0,   /* 默认 3 */

    .malloc_cb        = my_frame_malloc,
    .frame_display_cb = my_frame_display,
    .frame_consumed_cb = my_stream_free,   /* 可选 */
    .user_data        = &my_ctx,

    .decode_timeout_ms = 0,   /* 默认 1000ms */
    .task_priority     = 0,   /* 默认 BEKEN_DEFAULT_WORKER_PRIORITY */
    .task_stack_size   = 0,   /* 默认 4096 */
    .queue_depth       = 0,   /* 默认 1（JPEG 每次 push 前排空队列）；H264 至少 4 */
};
```

### 3.4 生命周期 API

5 个函数对应 4 个状态：

```
         create()          start()
  NULL ──────────▶ READY ──────────▶ RUNNING
                     ▲                  │
                     │    stop()        │
                     └──────────────────┘
                     │
                destroy()
                     │
                     ▼
                   NULL
```

| 函数 | 从状态 | 到状态 | 说明 |
|------|--------|--------|------|
| `create` | NULL | READY | 分配资源，按 `decode_type` 初始化对应解码器硬件 |
| `start` | READY | RUNNING | 创建 decode 任务线程，重跑后端激活策略 |
| `push_frame` | RUNNING | RUNNING | 推入一帧（JPEG 或 H264 AU，非阻塞，自动切换后端） |
| `stop` | RUNNING | READY | 停止任务，排空队列 |
| `destroy` | READY | NULL | 释放所有资源 |

---

## 第四章：内部实现详解

### 4.1 编解码后端架构

核心（`decode_stream_pipeline.c`）不含任何编解码分支，每个编解码实现为一张 `dsp_codec_ops_t` 后端表：

```
                    ┌─────────────────────────────┐
                    │   decode_stream_pipeline.c   │
                    │  队列 / decode task / GPU /   │
                    │  ring / 后端切换(codec_lock)  │
                    └──────────┬──────────────────┘
                               │ ctx->ops
              ┌────────────────┴────────────────┐
              ▼                                 ▼
   ┌──────────────────────┐         ┌──────────────────────┐
   │ dsp_jpeg_ops         │         │ dsp_h264_ops         │
   │ · VCDEC JPEG 解码器   │         │ · VCDEC H264 解码器   │
   │ · mjpegd bond        │         │ · h264d bond         │
   │ · SOF 解析分类        │         │ · NALU 分类(IDR/SPS)  │
   │ · latest-wins 准入   │         │ · GOP-aware 准入     │
   └──────────────────────┘         └──────────────────────┘
```

上下文同一时刻只有一个活跃后端 + 一个活跃 bond。每次 `push_frame` 重新分类帧，码流类型变化即双向切换；未选中类型的解码器**惰性创建**（首次切换时），因此纯 JPEG 会话不会占用 H264 解码器内存。

### 4.2 内部上下文（简化）

```c
struct decode_stream_pipeline_ctx {
    decode_stream_pipeline_config_t cfg;

    const dsp_codec_ops_t *ops;      /* 活跃后端 */

    /* 各编解码私有状态（核心代码不触碰） */
    dsp_jpeg_state_t jpeg;           /* { 解码器句柄 } */
    dsp_h264_state_t h264;           /* { 解码器句柄, GOP 丢帧状态 } */

    bk_gpu_ctlr_handle_t gpu_handle; /* GPU 控制器（编解码无关） */
    void *bond;                      /* 活跃 bond（属 ctx->ops） */

    /* FLEXA ring buffer（~135KB，PSRAM 64 字节对齐） */
    uint8_t  *ring_buf;
    void     *ring_raw;
    uint32_t  ring_size;

    /* 解码任务 */
    beken_thread_t    task_handle;
    beken_semaphore_t task_sem;
    beken_queue_t     frame_queue;
    beken_mutex_t     codec_lock;    /* 解码 / 后端切换互斥 */
    volatile uint8_t  running;
};
```

### 4.3 create() 初始化

```
Step 1: 分配 FLEXA ring buffer（PSRAM + 64字节对齐）
Step 2: 按 decode_type 选择初始后端，急切创建其解码器
        （AUTO → JPEG；另一编解码首次用到时才创建）
Step 3: 创建 GPU 控制器（flexa=true, src_buffer=ring_buf）
Step 4: 建立初始后端的 FLEXA bond
Step 5: 初始化帧队列（深度按 H264 最坏情况 ≥4）+ codec_lock
```

**错误回退：**任何步骤失败，按逆序释放已分配的资源（`goto err_xxx` 标签链）。

### 4.4 decode 任务线程

```c
static void dsp_decode_task(void *arg)
{
    rtos_set_semaphore(&ctx->task_sem);          /* 通知 start() 就绪 */

    while (ctx->running) {
        ret = rtos_pop_from_queue(&frame_queue, &entry, 50);
        if (ret != BK_OK) continue;              /* 超时，重查 running */

        rtos_lock_mutex(&ctx->codec_lock);       /* 与后端切换互斥 */
        ret = entry.ops->decode_frame(ctx, &entry);  /* 按入队时的后端快照分发 */
        rtos_unlock_mutex(&ctx->codec_lock);
        //   ↑ 阻塞直到全帧解码完成
        //   内部：VCDEC 逐 strip 写入 ring → bond 通知 GPU → GPU 逐 strip 处理
        //   最后 GPU 调 frame_display 输出完整帧

        if (frame_consumed_cb)
            frame_consumed_cb(entry.stream, ret, entry.owner, user_data);
    }

    rtos_set_semaphore(&ctx->task_sem);          /* 通知 stop() 退出 */
    rtos_delete_thread(NULL);
}
```

**关键点：**`decode_frame()` 是**阻塞调用**，但内部并非空等——VCDEC 硬件在解码的同时，GPU 已经通过 bond 开始处理已完成的 strip。当函数返回时，解码和 GPU 压缩都已完成，`frame_display` 回调也已经被触发。

### 4.5 后端切换（push 路径）

```
push_frame(stream, len, owner)
    │
    ├── 1. 分类：先试活跃后端 parse_meta，失败再试另一后端
    ├── 2. 类型变化 → dsp_activate_backend()（持 codec_lock）：
    │        drain 队列 → 拆旧 bond → 惰性建新解码器 → 建新 bond
    │        → on_activate（H264：等待首个 IDR）
    │        失败自动回退旧 bond
    ├── 3. 准入：JPEG → drain 队列（latest-wins）
    │         H264 → IDR 重同步 / 断链丢到下个 IDR
    └── 4. 入队（携带后端快照 ops）→ diag 日志
```

codec_lock 保证切换永远不会在旧解码器解码到一半时拆除其 bond（生产者最多等一帧的解码时长）。

### 4.6 对齐分配详解

VCDEC DMA 要求 ring buffer 64 字节对齐。`psram_malloc` 不保证对齐，所以需要手动处理：

```
psram_malloc 返回的 raw 指针：
┌──────────┬──────────┬──────────────────────────────┐
│ padding  │ raw ptr  │ 64-byte aligned user buffer   │
│ (0~63B)  │ (void*)  │ (ring_size bytes)             │
└──────────┴──────────┴──────────────────────────────┘
                ↑              ↑
            存储 raw       返回给调用者
            用于 free

// 分配
total = size + 64 - 1 + sizeof(void*);
raw = psram_malloc(total);
aligned = (raw + sizeof(void*) + 63) & ~63;
((void**)aligned)[-1] = raw;    // 在 aligned 前面存 raw

// 释放
raw = ((void**)aligned)[-1];
psram_free(raw);
```

### 4.7 destroy() 逆序释放

```
Step 1: running = 0, 等待 decode task 退出
Step 2: 排空 frame_queue（对残留帧触发 frame_consumed_cb 通知调用者）
Step 3: 拆活跃 bond                              ← 先断开信号
Step 4: bk_gpu_close / deinit / delete           ← 再关 GPU
Step 5: 经两张 ops 表拆除两个解码器（未用过的为 no-op）
Step 6: rtos_deinit_mutex / deinit_queue         ← 释放锁与队列
Step 7: dsp_psram_aligned_free(ring_buf)         ← 释放 ring buffer
Step 8: os_free(ctx)                             ← 释放上下文
```

顺序很重要：先断 bond（停止信号传递），再关 GPU（停止处理），再关解码器（停止硬件），最后释放内存。

---

## 第五章：与旧方案的对比

### 5.1 架构对比

```
旧方案（jpeg_decode_manager.c + gpu_compress_test.c）:

    VCDEC ──strip──▶ ISR callback ──memcpy──▶ s_linear_frame_buf (3MB)
                                                      │
                                              全帧拼完后
                                                      │
                                              gpu_dpu_block_demo_flush()
                                                      │
                                              手写 vg_lite_blit 循环
                                                      │
                                              bk_display_flush()

新方案（decode_stream_pipeline）:

    VCDEC ──strip──▶ [bond] ──▶ GPU ctlr（逐 strip 并行处理）
                                    │
                              全部 strip 完成
                                    │
                              frame_display_cb()
                                    │
                              调用者决定怎么显示
```

### 5.2 性能对比

| 指标 | 旧方案 | 新方案 |
|------|--------|--------|
| 每帧耗时 | ~2300ms (2000+300) | ~2000ms (GPU 隐藏在 VCDEC 之中) |
| 中间 memcpy | 3MB/帧 | 0（零拷贝） |
| ring buffer 内存 | 90KB + 3MB 线性帧 | 仅 ~135KB |
| LVGL 互斥 | 每帧 `lvgl_app_stop_for_casting()` ~30ms | 调用者启动前处理一次 |
| SDK API | 旧 `h264_decoder_api` | 现代 ctlr + bond |

---

## 第六章：使用示例

### 6.1 基本用法

```c
#include "decode_stream_pipeline.h"
#include <components/bk_frame_buffer.h>
#include <components/bk_display.h>
#include <components/media_data_process_que.h>
#include <common/avdk_pixel_types.h>

/* GPU 输出帧分配器 */
static void *my_frame_malloc(uint32_t size)
{
    return bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
}

/* 输出帧就绪回调 —— 送 DPU 显示 */
static void my_frame_display(void *frame, uint32_t frame_size, void *user_data)
{
    bk_display_ctlr_handle_t disp = (bk_display_ctlr_handle_t)user_data;
    bk_display_flush(disp, frame, my_frame_free_cb);
}

/* 码流消费完毕回调：owner 与 push_frame 第四参一致（此处为 frame_buffer_t*） */
static void my_stream_consumed(const uint8_t *stream, int status, void *owner,
                               void *user_data)
{
    (void)stream;
    (void)user_data;
    if (owner)
        media_frame_queue_free((frame_buffer_t *)owner);
}

/* 初始化 */
void casting_start(bk_display_ctlr_handle_t display)
{
    decode_stream_pipeline_config_t cfg = {
        .decode_type     = DECODE_STREAM_TYPE_AUTO,  /* 投屏源 JPEG/H264 混合 */
        .src_width       = 1920,
        .src_height      = 1080,
        .dst_width       = 1080,
        .dst_height      = 1920,
        .rotate_degree   = 90,
        .dst_format      = BK_PIXEL_FORMAT_ARGB8888,
        .compress        = true,
        .malloc_cb       = my_frame_malloc,
        .frame_display_cb = my_frame_display,
        .frame_consumed_cb = my_stream_consumed,
        .user_data       = display,
    };

    decode_stream_pipeline_handle_t pipeline;
    decode_stream_pipeline_create(&pipeline, &cfg);
    decode_stream_pipeline_start(pipeline);

    /* 保存 handle，后续推帧用 */
    g_pipeline = pipeline;
}

/* 收到网络帧：码流为 fb->frame，第四参传 fb 供回调中释放。
   JPEG 帧与 H264 访问单元都可以直接推，管线自动分类切换 */
void on_frame_received(frame_buffer_t *fb)
{
    if (fb == NULL || fb->frame == NULL)
        return;
    decode_stream_pipeline_push_frame(g_pipeline, fb->frame, fb->length, fb);
}

/* 停止投屏 */
void casting_stop(void)
{
    decode_stream_pipeline_stop(g_pipeline);
    decode_stream_pipeline_destroy(g_pipeline);
    g_pipeline = NULL;
}
```

### 6.2 错误处理

```c
avdk_err_t ret = decode_stream_pipeline_create(&pipeline, &cfg);
if (ret == AVDK_ERR_NOMEM) {
    /* ring buffer 或上下文分配失败 */
    BK_LOGE("cast", "pipeline create OOM\n");
}
else if (ret == AVDK_ERR_INVAL) {
    /* 配置参数错误（如 malloc_cb 为 NULL） */
    BK_LOGE("cast", "pipeline invalid config\n");
}

ret = decode_stream_pipeline_push_frame(pipeline, data, len, NULL);
if (ret == AVDK_ERR_BUSY) {
    /* 队列满或 GOP 断链丢帧 —— 这是正常的背压表现，不需要报错 */
}
```

---

## 第七章：设计决策 Q&A

### Q1: 为什么 `push_frame` 是非阻塞的？

因为网络接收任务不能被解码阻塞。如果 VCDEC 正在解码（~2000ms），网络接收任务需要继续接收后续帧。队列满时直接丢弃（JPEG 丢最旧 / H264 断链丢到下个 IDR），确保不会拖慢网络接收。

### Q2: 为什么用 `frame_consumed_cb` 而不是直接在 `push_frame` 后释放？

因为 `push_frame` 只是入队，VCDEC 可能还没开始读这块码流。如果调用者在 `push_frame` 返回后立即释放码流，VCDEC 读到的将是无效数据。`frame_consumed_cb` 在解码返回后才触发，此时码流已经被 VCDEC 完全消费。

### Q3: 为什么 `create` 和 `start` 分开？

允许调用者在 `create` 之后、`start` 之前做额外准备（如停止 LVGL、配置 DPU 层），而不需要在时间紧迫的情况下做这些操作。

### Q4: 为什么 `flexa_done_cb` 设为 NULL？

因为 bond start 会通过 ioctl 注入自己的 `flexa_done_cb`。如果我们手动设置了回调，会和 bond 冲突。

### Q5: 为什么 GPU 的 `free` 回调设为 NULL？

参考 doorbell 的 `app_gpu.c`。GPU 控制器内部的工作方式是：每帧开始时用 `malloc` 分配新 buffer，帧完成后通过 `frame_display` 把旧 buffer 交出去。GPU 自己不需要 free 任何 buffer——交出去的 buffer 由接收方（调用者）负责释放。

### Q6: ring buffer 用 hsram 还是 psram？

用 PSRAM。早期版本尝试过 HSRAM ring，现场出现 MemFault（HSRAM 需保留给 `bk_gpu_init` 内部的 `bk_get_gpu_flexa_buffer`）；PSRAM ring 经过了 VCDEC FLEXA DMA 验证（SDK `jpeg_decoder_flexa_test` 同路径）。

### Q7: 为什么 H264 准入要"断链丢到下个 IDR"？

H264 帧间有参考依赖：丢了任何一帧，后续 P/B 帧引用的参考帧就不存在，强行解码会输出花屏/拖影。丢到下个 IDR（可独立解码）再恢复，画面只会冻结一瞬间，比连续花屏体验好。可用 `DSP_H264_GOP_DROP_ENABLE` 编译开关关闭该行为（用于和手机端丢帧对账）。

---

## 附录：文件清单

```
decode_stream_pipeline/
├── include/
│   └── decode_stream_pipeline.h          # 公开接口（5个API + 3个回调 + 配置）
├── src/
│   ├── decode_stream_pipeline.c          # 核心（队列/任务/GPU/ring/切换）
│   ├── decode_stream_pipeline_internal.h # 后端接口 + ctx（组件内部）
│   ├── decode_stream_pipeline_jpeg.c     # JPEG 后端
│   └── decode_stream_pipeline_h264.c     # H264 后端
├── doc/
│   └── tutorial.md                       # 本文档
├── CMakeLists.txt                        # 编译配置
└── DESIGN.md                             # 架构设计文档
```
