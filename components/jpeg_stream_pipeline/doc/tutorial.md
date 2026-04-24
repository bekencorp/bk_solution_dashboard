# jpeg_stream_pipeline 组件详解教程

由浅入深讲解组件的设计思路、SDK 基础、内部实现和使用方法。

---

## 第一章：这个组件解决什么问题？

### 1.1 应用场景

摩托车/电动车仪表盘通过 WiFi 从手机接收 JPEG 图片流（导航画面），解码后显示在 1920x1080 的 MIPI DSI 屏幕上。

数据流路径：

```
手机 App（导航）
   │  WiFi
   ▼
BK7259 AP 侧接收 JPEG 码流
   │
   ▼
VCDEC 硬件解码（JPEG → NV12 原始像素）
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

`jpeg_stream_pipeline` 利用 SDK 内置的 strip 级并行能力：

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

BK7259 内置 VCDEC（Video Codec Decoder）硬件，支持 JPEG 解码。

**FLEXA 模式**是一种流式输出模式：VCDEC 不一次输出整帧，而是每解出 16 行像素（一个 "strip"），就写入 ring buffer 的一个槽位，然后触发回调。

```
JPEG 码流 ──▶ VCDEC 硬件
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
          = 1920 * 16 * 1.5 * 2
          = 92,160 字节 ≈ 90KB
```

其中 `1.5` 是 NV12 格式每像素字节数（Y 平面 1 字节 + UV 平面 0.5 字节），`buff_cnt=2` 表示 2 个槽位交替使用。

**关键 API：**
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
.flexa_buff_cnt = 2          // ring buffer 2 个槽
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
GPU 处理完一个 strip 后调用 `bk_jpeg_decode_ioctl(jpeg, SET_RD_PTR, ...)`，通知 VCDEC 可以复用该 ring 槽。

这就是为什么在创建 JPEG 解码器时 `flexa_done_cb = NULL`——回调由 bond 注入，无需手动设置。

**API：**
```c
bk_flexa_mjpegd_gpu_bond_start(&bond, jpeg_handle, gpu_handle);  // 建立连接
bk_flexa_mjpegd_gpu_bond_stop(bond);                              // 断开连接
```

---

## 第三章：公开接口解析

### 3.1 数据类型

```c
/* 不透明 handle —— 调用者只拿到指针，看不到内部结构 */
typedef struct jpeg_stream_pipeline_ctx *jpeg_stream_pipeline_handle_t;
```

### 3.2 三个回调

| 回调 | 必需？ | 运行上下文 | 作用 |
|------|--------|-----------|------|
| `jsp_frame_display_cb_t` | 是 | GPU 任务 | 输出帧就绪，接收方负责释放帧 |
| `jsp_malloc_cb_t` | 是 | GPU 任务 | 为 GPU 分配输出帧 buffer |
| `jsp_frame_consumed_cb_t` | 否 | decode 任务 | 通知 JPEG 码流已消费；回调含 `jpeg_owner`（`push_frame_ex` 传入，可为 NULL） |

**内存所有权流转图：**

```
调用者分配 JPEG 码流 ──push_frame / push_frame_ex──▶ pipeline 使用（零拷贝）
                                         │
                              bk_jpeg_decode_frame 完成
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
jpeg_stream_pipeline_config_t cfg = {
    /* 源帧尺寸 —— JPEG 图像实际分辨率 */
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
    .flexa_buff_cnt = 0,   /* 默认 2 */

    .malloc_cb        = my_frame_malloc,
    .frame_display_cb = my_frame_display,
    .frame_consumed_cb = my_stream_free,   /* 可选 */
    .user_data        = &my_ctx,

    .decode_timeout_ms = 0,   /* 默认 1000ms */
    .task_priority     = 0,   /* 默认 BEKEN_DEFAULT_WORKER_PRIORITY */
    .task_stack_size   = 0,   /* 默认 4096 */
    .queue_depth       = 0,   /* 默认 4 */
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
| `create` | NULL | READY | 分配资源，初始化硬件 |
| `start` | READY | RUNNING | 创建 decode 任务线程 |
| `push_frame` | RUNNING | RUNNING | 推入 JPEG 帧（非阻塞） |
| `push_frame_ex` | RUNNING | RUNNING | 同上，并附带每帧 `jpeg_owner`（如 `frame_buffer_t *`）供 consumed 回调释放 |
| `stop` | RUNNING | READY | 停止任务，排空队列 |
| `destroy` | READY | NULL | 释放所有资源 |

---

## 第四章：内部实现详解

### 4.1 内部上下文

```c
struct jpeg_stream_pipeline_ctx {
    jpeg_stream_pipeline_config_t cfg;     /* 配置副本 */

    /* 三个硬件句柄 */
    bk_jpeg_decode_ctlr_handle_t  jpeg_handle;  /* VCDEC 解码器 */
    bk_gpu_ctlr_handle_t          gpu_handle;   /* GPU 控制器 */
    void                         *bond;          /* FLEXA bond */

    /* FLEXA ring buffer（~90KB，hsram 64 字节对齐） */
    uint8_t  *ring_buf;
    void     *ring_raw;          /* 原始指针，用于 free */
    uint32_t  ring_size;

    /* 解码任务 */
    beken_thread_t    task_handle;
    beken_semaphore_t task_sem;      /* 任务启动/退出同步 */
    beken_queue_t     frame_queue;   /* JPEG 帧输入队列 */
    volatile uint8_t  running;       /* 0=停止，1=运行 */
};
```

### 4.2 create() 五步初始化

```
Step 1: 分配 FLEXA ring buffer
        ┌─────────────────────────────────────┐
        │  hsram_malloc + 64字节对齐            │
        │  大小 = aligned_w * 16 * 1.5 * 2     │
        │  例: 1920 * 16 * 1.5 * 2 = 92160字节 │
        └─────────────────────────────────────┘
                        │
                        ▼
Step 2: 创建 JPEG 解码器
        ┌─────────────────────────────────────┐
        │  bk_jpeg_decode_new()                │
        │    mode = FLEXA                      │
        │    segment_height = 1 (16行/段)       │
        │    segment_number = 2 (ring 2槽)      │
        │    flexa_done_cb = NULL (bond注入)     │
        │  bk_jpeg_decode_init()               │
        │  bk_jpeg_decode_open()               │
        └─────────────────────────────────────┘
                        │
                        ▼
Step 3: 创建 GPU 控制器
        ┌─────────────────────────────────────┐
        │  bk_gpu_ctlr_new()                   │
        │    flexa = true                      │
        │    src_buffer = ring_buf             │
        │    frame_display = 内部转发函数       │
        │  bk_gpu_init()                       │
        │  bk_gpu_open()                       │
        └─────────────────────────────────────┘
                        │
                        ▼
Step 4: 建立 FLEXA bond
        ┌─────────────────────────────────────┐
        │  bk_flexa_mjpegd_gpu_bond_start()    │
        │    连接 VCDEC ←→ GPU 信号路由         │
        └─────────────────────────────────────┘
                        │
                        ▼
Step 5: 初始化帧队列
        ┌─────────────────────────────────────┐
        │  rtos_init_queue()                   │
        │    元素 = {指针, 长度}                │
        │    深度 = 4                          │
        └─────────────────────────────────────┘
```

**错误回退：**任何步骤失败，按逆序释放已分配的资源。代码中使用 `goto err_xxx` 标签链实现：

```c
err_stop_bond:   → bk_flexa_mjpegd_gpu_bond_stop
err_close_gpu:   → bk_gpu_close
err_deinit_gpu:  → bk_gpu_deinit
err_delete_gpu:  → bk_gpu_delete
err_close_jpeg:  → bk_jpeg_decode_close
err_deinit_jpeg: → bk_jpeg_decode_deinit
err_delete_jpeg: → bk_jpeg_decode_delete
err_free_ring:   → jsp_aligned_free
err_free_ctx:    → os_free
```

### 4.3 decode 任务线程

```c
static void jsp_decode_task(void *arg)
{
    /* 1. 通知 start() 任务已就绪 */
    rtos_set_semaphore(&ctx->task_sem);

    while (ctx->running) {
        /* 2. 从队列取帧，50ms 超时 */
        ret = rtos_pop_from_queue(&frame_queue, &entry, 50);
        if (ret != BK_OK) continue;  // 超时，重新检查 running

        /* 3. 提交给 VCDEC 解码 */
        input.stream     = entry.jpeg_stream; // 零拷贝
        input.out_buffer = ctx->ring_buf;   // FLEXA ring buffer
        bk_jpeg_decode_frame(jpeg_handle, &input);
        //   ↑ 阻塞直到全帧解码完成
        //   内部：VCDEC 逐 strip 写入 ring → bond 通知 GPU → GPU 逐 strip 处理
        //   最后 GPU 调 frame_display 输出完整帧

        /* 4. 通知调用者码流可释放 */
        if (frame_consumed_cb) frame_consumed_cb(entry.jpeg_stream, status, entry.jpeg_owner, user_data);
    }

    /* 5. 通知 stop() 任务已退出 */
    rtos_set_semaphore(&ctx->task_sem);
    rtos_delete_thread(NULL);
}
```

**关键点：**`bk_jpeg_decode_frame()` 是**阻塞调用**，但内部并非空等——VCDEC 硬件在解码的同时，GPU 已经通过 bond 开始处理已完成的 strip。当函数返回时，解码和 GPU 压缩都已完成，`frame_display` 回调也已经被触发。

### 4.4 帧数据全流程（时序图）

```
           调用者              decode task           VCDEC HW          Bond           GPU ctlr
           ──────              ───────────           ────────          ────           ────────
              │                     │                   │                │               │
push_frame(_ex)(jpeg, len, owner)   │                   │                │               │
  │─── queue push ──▶│              │                   │                │               │
  │                  │              │                   │                │               │
  │            queue pop ──▶│       │                   │                │               │
  │                         │       │                   │                │               │
  │              bk_jpeg_decode_frame(stream, ring_buf) │                │               │
  │                         │──────▶│                   │                │               │
  │                         │       │  解码 strip 0     │                │               │
  │                         │       │──写入 ring[0]────▶│                │               │
  │                         │       │                   │─ioctl(READY)─▶│               │
  │                         │       │                   │               │─ blit strip 0 │
  │                         │       │  解码 strip 1     │               │   (并行!)      │
  │                         │       │──写入 ring[1]────▶│               │               │
  │                         │       │                   │               │──done──▶释放 ring[0]
  │                         │       │                   │─ioctl(READY)─▶│               │
  │                         │       │  解码 strip 2     │               │─ blit strip 1 │
  │                         │       │──写入 ring[0]     │               │   (ring[0]已释放)
  │                         │       │  (ring[0]已释放)  │               │               │
  │                         │       │     ...           │     ...       │     ...       │
  │                         │       │                   │               │               │
  │                         │       │  最后一个 strip    │               │               │
  │                         │       │──────────────────▶│               │               │
  │                         │       │                   │─ioctl(READY)─▶│               │
  │                         │       │                   │               │─ blit 最后strip│
  │                         │       │                   │               │               │
  │                         │       │                   │               │──frame_display─▶
  │                         │       │                   │               │    (输出帧就绪)
  │◀────────────────────────│───────│───────────────────│───────────────│────callback───│
  │  frame_display_cb(frame, size, user_data)           │               │               │
  │                         │       │                   │               │               │
  │              ◀──返回────│       │                   │               │               │
  │  frame_consumed_cb(jpeg_stream, 0, jpeg_owner, user_data) │               │               │
  │◀────────────────────────│       │                   │               │               │
  │  (可释放 JPEG 码流)     │       │                   │               │               │
```

### 4.5 对齐分配详解

VCDEC DMA 要求 ring buffer 64 字节对齐。`hsram_malloc` 不保证对齐，所以需要手动处理：

```
hsram_malloc 返回的 raw 指针：
┌──────────┬──────────┬──────────────────────────────┐
│ padding  │ raw ptr  │ 64-byte aligned user buffer   │
│ (0~63B)  │ (void*)  │ (ring_size bytes)             │
└──────────┴──────────┴──────────────────────────────┘
                ↑              ↑
            存储 raw       返回给调用者
            用于 free

// 分配
total = size + 64 - 1 + sizeof(void*);
raw = hsram_malloc(total);
aligned = (raw + sizeof(void*) + 63) & ~63;
((void**)aligned)[-1] = raw;    // 在 aligned 前面存 raw

// 释放
raw = ((void**)aligned)[-1];
os_free(raw);
```

### 4.6 destroy() 逆序释放

```
Step 1: running = 0, 等待 decode task 退出
Step 2: 排空 frame_queue（对残留帧触发 frame_consumed_cb 通知调用者）
Step 3: bk_flexa_mjpegd_gpu_bond_stop(bond)     ← 先断开信号
Step 4: bk_gpu_close / deinit / delete           ← 再关 GPU
Step 5: bk_jpeg_decode_close / deinit / delete   ← 再关解码器
Step 6: rtos_deinit_queue                        ← 释放队列
Step 7: jsp_aligned_free(ring_buf)               ← 释放 ring buffer
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

新方案（jpeg_stream_pipeline）:

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
| ring buffer 内存 | 90KB + 3MB 线性帧 | 仅 90KB |
| LVGL 互斥 | 每帧 `lvgl_app_stop_for_casting()` ~30ms | 调用者启动前处理一次 |
| 代码量 | ~700行 (两个文件) | ~330行 (一个文件) |
| SDK API | 旧 `h264_decoder_api` | 现代 ctlr + bond |

### 5.3 关键改进解释

**为什么消除了 memcpy？**

旧方案中 `jpeg_flexa_done_callback` 把每条 strip 从 FLEXA ring 拷贝到 `s_linear_frame_buf`，是因为 GPU 需要看到完整的线性帧。

新方案中 GPU 控制器通过 `gpu_flexa_addr_mapping` 建立了硬件地址映射，将 FLEXA ring buffer 的循环地址空间映射到 GPU 的虚拟线性地址空间（`GPU_Y_VADDR_BASE = 0x38200000`）。GPU 直接通过虚拟地址访问 ring buffer，硬件自动处理环形回绕，不需要 CPU 搬运。

---

## 第六章：使用示例

### 6.1 基本用法

```c
#include "jpeg_stream_pipeline.h"
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

/* 码流消费完毕回调：jpeg_owner 与 push_frame_ex 第四参一致（此处为 frame_buffer_t*） */
static void my_stream_consumed(const uint8_t *jpeg_stream, int status, void *jpeg_owner,
                               void *user_data)
{
    (void)jpeg_stream;
    (void)user_data;
    if (jpeg_owner)
        media_frame_queue_free((frame_buffer_t *)jpeg_owner);
}

/* 初始化 */
void casting_start(bk_display_ctlr_handle_t display)
{
    jpeg_stream_pipeline_config_t cfg = {
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

    jpeg_stream_pipeline_handle_t pipeline;
    jpeg_stream_pipeline_create(&pipeline, &cfg);
    jpeg_stream_pipeline_start(pipeline);

    /* 保存 handle，后续推帧用 */
    g_pipeline = pipeline;
}

/* 收到网络帧：码流为 fb->frame，第四参传 fb 供回调中释放 */
void on_jpeg_frame_received(frame_buffer_t *fb)
{
    if (fb == NULL || fb->frame == NULL)
        return;
    jpeg_stream_pipeline_push_frame(g_pipeline, fb->frame, fb->length, fb);
}

/* 停止投屏 */
void casting_stop(void)
{
    jpeg_stream_pipeline_stop(g_pipeline);
    jpeg_stream_pipeline_destroy(g_pipeline);
    g_pipeline = NULL;
}
```

### 6.2 错误处理

```c
avdk_err_t ret = jpeg_stream_pipeline_create(&pipeline, &cfg);
if (ret == AVDK_ERR_NOMEM) {
    /* ring buffer 或上下文分配失败 */
    BK_LOGE("cast", "pipeline create OOM\n");
}
else if (ret == AVDK_ERR_INVAL) {
    /* 配置参数错误（如 malloc_cb 为 NULL） */
    BK_LOGE("cast", "pipeline invalid config\n");
}

ret = jpeg_stream_pipeline_push_frame(pipeline, data, len, NULL);
if (ret == AVDK_ERR_BUSY) {
    /* 队列满，帧被丢弃 —— 这是正常的背压表现，不需要报错 */
}
```

---

## 第七章：设计决策 Q&A

### Q1: 为什么 `push_frame` 是非阻塞的？

因为网络接收任务不能被解码阻塞。如果 VCDEC 正在解码（~2000ms），网络接收任务需要继续接收后续帧。队列满时直接丢弃最新帧（而非阻塞），确保不会拖慢网络接收。

### Q2: 为什么用 `frame_consumed_cb` 而不是直接在 `push_frame` 后释放？

因为 `push_frame` 只是入队，VCDEC 可能还没开始读这块码流。如果调用者在 `push_frame` 返回后立即释放码流，VCDEC 读到的将是无效数据。`frame_consumed_cb` 在 `bk_jpeg_decode_frame` 返回后才触发，此时码流已经被 VCDEC 完全消费。

### Q3: 为什么 `create` 和 `start` 分开？

允许调用者在 `create` 之后、`start` 之前做额外准备（如停止 LVGL、配置 DPU 层），而不需要在时间紧迫的情况下做这些操作。

### Q4: 为什么 `flexa_done_cb` 设为 NULL？

因为 `bk_flexa_mjpegd_gpu_bond_start()` 会通过 `bk_jpeg_decode_ioctl(REGISTER_BOND)` 注入自己的 `flexa_done_cb`。如果我们手动设置了回调，会和 bond 冲突。

### Q5: 为什么 GPU 的 `free` 回调设为 NULL？

参考 doorbell 的 `app_gpu.c`。GPU 控制器内部的工作方式是：每帧开始时用 `malloc` 分配新 buffer，帧完成后通过 `frame_display` 把旧 buffer 交出去。GPU 自己不需要 free 任何 buffer——交出去的 buffer 由接收方（调用者）负责释放。

### Q6: ring buffer 用 hsram 还是 psram？

优先 hsram（高速 SRAM），因为 VCDEC DMA 和 GPU 地址映射对 hsram 的访问延迟更低。doorbell 参考实现也使用 hsram。如果 hsram 不足，可以回退到 psram（代码中可扩展此逻辑）。

---

## 附录：文件清单

```
jpeg_stream_pipeline/
├── include/
│   └── jpeg_stream_pipeline.h   # 公开接口（5个API + 3个回调 + 配置结构体）
├── src/
│   └── jpeg_stream_pipeline.c   # 实现（~330行）
├── doc/
│   └── tutorial.md              # 本文档
├── CMakeLists.txt               # 编译配置
└── DESIGN.md                    # 架构设计文档
```
