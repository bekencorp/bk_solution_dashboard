# jpeg_stream_pipeline 组件设计文档

## 1. 概述

`jpeg_stream_pipeline` 是一个高层组件，封装「JPEG 帧输入 → VCDEC FLEXA 解码 → GPU strip 级并行压缩 → DPU 显示帧输出」完整流水线。

### 1.1 设计动机

当前 `decode` 组件（`jpeg_decode_manager.c` + `gpu_compress_test.c`）存在以下问题：

| 问题 | 说明 |
|------|------|
| 全帧串行 | VCDEC 解完全部 strip → memcpy 拼成线性帧 → GPU 逐块压缩 → DPU 显示，三阶段完全串行 |
| 手写 strip 拼帧 | `jpeg_flexa_done_callback` 将每条 strip 从 ring buffer memcpy 到 `s_linear_frame_buf`（~3MB），浪费带宽 |
| 未使用 SDK GPU 控制器 | 手写 `vg_lite_blit` 循环，未利用 `bk_gpu_ctlr` 的 FLEXA strip 并行能力 |
| 旧解码 API | 使用 `h264_decoder_api.h` 中的 `jpeg_decoder_init/decode/deinit`，非现代 ctlr 风格 |
| 每帧停 LVGL | 每帧调用 `lvgl_app_stop_for_casting()` 引入 ~30ms 开销 |

SDK 中 `bk_gpu_ctlr`（`bk_gpu_ctlr_default.c`）已实现 strip 级 VCDEC+GPU 并行流水线，含旋转和 DMA 支持，doorbell 工程已验证可用。新组件直接利用这一能力。

### 1.2 设计原则

- **独立性**：不修改任何现有组件，不依赖 `decode` 组件
- **不透明 handle**：内部实现细节完全隐藏
- **回调驱动**：输出帧通过回调交付，不绑定具体显示逻辑
- **内存所有权清晰**：谁分配谁释放，接口文档明确标注

---

## 2. SDK 基础设施

新组件基于以下 SDK 能力构建，不重复造轮子：

### 2.1 bk_jpeg_decode_ctlr（VCDEC FLEXA 解码器控制器）

- 头文件：`<components/bk_decode/bk_jpeg_decode_ctlr.h>`
- 参考实现：`avdk_smp_sdk/projects/common_components/doorbell_device_service/src/app_jpeg_decode.c`

```
bk_jpeg_decode_new(handle, config)   → 创建实例
bk_jpeg_decode_init(handle)          → 初始化硬件
bk_jpeg_decode_open(handle)          → 打开
bk_jpeg_decode_frame(handle, input)  → 提交一帧解码
bk_jpeg_decode_close/deinit/delete   → 关闭销毁
```

配置关键字段：
- `decode_mode = BK_JPEG_DECODE_FLEXA_MODE_FLEXA`
- `segment_height = 1`（每 16 行一个 segment）
- `segment_number = 2`（ring buffer 2 个槽）
- `flexa_done_cb`：由 bond 机制注入，不手动设置

### 2.2 bk_gpu_ctlr（GPU strip 并行控制器）

- 头文件：`<components/bk_gpu_ctlr.h>`
- 实现：`avdk_smp_sdk/ap/components/bk_gpu/src/bk_gpu_ctlr_default.c`

内部工作流程（每条 16 行 strip）：
```
VCDEC 解码 strip N → FLEXA ring slot 写入
    ↓ (bond 通知)
GPU ioctl(LINES_READY, wr_ptr)
    ↓
gpu_isp_line_done_handle() → vg_lite_blit(strip) + vg_lite_finish()
    ↓
DMA 搬运到输出 buffer（旋转时按列写入）
    ↓ (strip 处理完毕)
gpu_flexa_done → 通知解码器释放 ring slot
    ↓ (全部 strip 完成)
frame_display 回调 → 输出帧就绪
```

配置关键字段：
- `flexa = true`
- `flexa_lines = 16`
- `flexa_buff_cnt = 2`
- `src_buffer = flexa_ring_buf`（ring buffer 基地址）
- `rotate_degree = 0 或 90`
- `compress = true`（DPU HV sample 压缩）
- `frame_display`：帧完成回调
- `malloc`：输出帧分配器

### 2.3 bk_flexa_mjpegd_gpu_bond（FLEXA Bond 机制）

- 实现：`avdk_smp_sdk/ap/components/media_service/src/bk_flexa_mjpeg_bond.c`
- 头文件路径待确认（可能为 `<bk_flexa_bond.h>` 或 `<bk_flexa_bond_types.h>`）

Bond 建立 JPEG 解码器与 GPU 控制器的双向信号链路：

```
正向：解码器 flexa_done(wr_ptr)
       → gpu_bond_mjpegd_flexa_done()
       → bk_gpu_ioctl(gpu, BK_GPU_IOCTL_SET_FLEXA_LINES_READY, wr_ptr)

反向：GPU flexa_done(rd_cnt)
       → gpu_bond_gpu_flexa_done()
       → bk_jpeg_decode_ioctl(jpeg, BK_JPEG_DECODE_IOCTL_PORT_SET_RD_PTR, rd_cmd)
```

调用方式：
```c
bk_flexa_mjpegd_gpu_bond_start(&bond, jpeg_handle, gpu_handle);  // 建立
bk_flexa_mjpegd_gpu_bond_stop(bond);                              // 拆除
```

---

## 3. 组件结构

```
jpeg_stream_pipeline/
├── include/
│   └── jpeg_stream_pipeline.h   # 公开接口
├── src/
│   └── jpeg_stream_pipeline.c   # 实现
└── CMakeLists.txt
```

---

## 4. 公开接口设计

### 4.1 回调类型

```c
/**
 * 输出帧就绪回调。
 * 运行于 GPU 内部任务上下文，禁止阻塞操作。
 * 帧所有权转交给调用者，调用者负责释放。
 */
typedef void (*jsp_frame_display_cb_t)(void *frame, uint32_t frame_size, void *user_data);

/**
 * GPU 输出帧分配器。
 * 返回的 buffer 需满足 DPU 要求（PSRAM，对齐）。
 */
typedef void *(*jsp_malloc_cb_t)(uint32_t size);
```

### 4.2 配置结构体

```c
typedef struct {
    /* 源帧分辨率（JPEG 图像尺寸） */
    uint16_t src_width;
    uint16_t src_height;

    /* 目标分辨率（GPU 输出 / 显示分辨率） */
    uint16_t dst_width;
    uint16_t dst_height;

    /* GPU 参数 */
    uint16_t rotate_degree;           /* 0 或 90 */
    bk_pixel_format_t dst_format;     /* 通常 BK_PIXEL_FORMAT_ARGB8888 */
    bool     compress;                /* DPU HV sample 压缩 */
    bool     scale;                   /* GPU 缩放 */

    /* FLEXA ring buffer 参数 */
    uint8_t  flexa_lines;             /* 固定 16，填 0 使用默认值 */
    uint8_t  flexa_buff_cnt;          /* 固定 2，填 0 使用默认值 */

    /* 内存管理 */
    jsp_malloc_cb_t        malloc_cb;          /* 不能为 NULL */
    jsp_frame_display_cb_t frame_display_cb;   /* 不能为 NULL */
    void *user_data;                           /* 透传给回调 */

    /* 解码任务参数（填 0 使用默认值） */
    uint32_t decode_timeout_ms;       /* 默认 1000ms */
    uint32_t task_priority;           /* 默认 BEKEN_DEFAULT_WORKER_PRIORITY */
    uint32_t task_stack_size;         /* 默认 4096 */
} jpeg_stream_pipeline_config_t;
```

### 4.3 生命周期 API

```c
/* 不透明 handle */
typedef struct jpeg_stream_pipeline_t *jpeg_stream_pipeline_handle_t;

/* 创建：分配资源，初始化 VCDEC + GPU + Bond */
avdk_err_t jpeg_stream_pipeline_create(jpeg_stream_pipeline_handle_t *handle,
                                        const jpeg_stream_pipeline_config_t *config);

/* 启动：创建解码线程，开始接受帧 */
avdk_err_t jpeg_stream_pipeline_start(jpeg_stream_pipeline_handle_t handle);

/* 推帧并附带每帧 jpeg_owner（如 frame_buffer_t*），在 frame_consumed_cb 中原样返回 */
avdk_err_t jpeg_stream_pipeline_push_frame(jpeg_stream_pipeline_handle_t handle,
                                               const uint8_t *jpeg_stream,
                                               uint32_t jpeg_len,
                                               void *jpeg_owner);

/* 停止：停止接受新帧，等待当前帧处理完毕 */
avdk_err_t jpeg_stream_pipeline_stop(jpeg_stream_pipeline_handle_t handle);

/* 销毁：释放所有资源，handle 不再有效 */
avdk_err_t jpeg_stream_pipeline_destroy(jpeg_stream_pipeline_handle_t handle);
```

---

## 5. 内部实现架构

### 5.1 上下文结构体（不透明）

```c
typedef struct jpeg_stream_pipeline_t {
    jpeg_stream_pipeline_config_t config;     /* 深拷贝的配置 */

    /* 硬件句柄 */
    bk_jpeg_decode_ctlr_handle_t  jpeg_handle;
    bk_gpu_ctlr_handle_t          gpu_handle;
    void                         *bond;

    /* FLEXA ring buffer */
    uint8_t  *flexa_ring_buf;        /* 64 字节对齐 */
    void     *flexa_ring_raw_ptr;    /* 原始指针，用于 free */
    uint32_t  flexa_ring_size;

    /* 解码任务 */
    beken_thread_t    decode_task;
    beken_semaphore_t task_sem;
    beken_queue_t     frame_queue;
    volatile uint8_t  running;
} jpeg_stream_pipeline_ctx_t;
```

### 5.2 初始化顺序（create）

```
Step 1  os_malloc(sizeof(ctx))                    → 上下文
Step 2  aligned_hsram_alloc(64, ring_size)        → flexa_ring_buf
        ring_size = aligned_w * flexa_lines * 1.5 * flexa_buff_cnt
Step 3  bk_jpeg_decode_new / init / open          → jpeg_handle
        mode=FLEXA, segment_height=1, segment_number=2
Step 4  bk_gpu_ctlr_new / init / open             → gpu_handle
        flexa=true, src_buffer=flexa_ring_buf
Step 5  bk_flexa_mjpegd_gpu_bond_start            → bond
        自动连接双向信号链路
Step 6  rtos_init_queue                            → frame_queue
```

错误回退：任何步骤失败，逆序释放已分配的资源。

### 5.3 解码任务（decode task）

```c
while (running) {
    ret = rtos_pop_from_queue(&frame_queue, &entry, 50ms);
    if (ret != BK_OK) continue;  // 超时，重新检查 running

    input.stream     = entry.jpeg_stream;
    input.stream_len = entry.jpeg_len;
    input.out_buffer = flexa_ring_buf;

    bk_jpeg_decode_frame(jpeg_handle, &input);
    // VCDEC 开始解码 → 逐 strip 产出 → bond 通知 GPU → GPU 逐 strip 处理
    // 全部 strip 完成后 GPU 自动调用 frame_display 回调
}
```

### 5.4 帧数据流

```
调用者                          jpeg_stream_pipeline                 SDK
──────                         ──────────────────                   ────
push_frame(jpeg_stream, jpeg_len)
    │
    ├─── rtos_push_to_queue ──→ frame_queue
    │                              │
    │                      decode_task 取帧
    │                              │
    │                   bk_jpeg_decode_frame ────→ VCDEC FLEXA 解码
    │                              │                    │
    │                              │              strip 0 就绪
    │                              │                    │ (bond)
    │                              │              GPU blit strip 0
    │                              │              GPU 释放 ring slot 0
    │                              │                    │
    │                              │              strip 1 就绪
    │                              │                    │ (bond)
    │                              │              GPU blit strip 1 (与 VCDEC strip 2 并行!)
    │                              │                    │
    │                              │                  ......
    │                              │                    │
    │                              │              全部 strip 完成
    │                              │                    │
    │                   jsp_gpu_frame_display ←──── frame_display()
    │                              │
frame_display_cb(frame) ←──────────┘
    │
调用者送 DPU 显示并释放帧
```

### 5.5 销毁顺序（destroy）

```
Step 1  running = 0，等待 decode_task 退出
Step 2  排空 frame_queue
Step 3  bk_flexa_mjpegd_gpu_bond_stop(bond)
Step 4  bk_gpu_close / deinit / delete
Step 5  bk_jpeg_decode_close / deinit / delete
Step 6  rtos_deinit_queue
Step 7  aligned_hsram_free(flexa_ring_buf)
Step 8  os_free(ctx)
```

---

## 6. 内存管理

### 6.1 FLEXA Ring Buffer

- **大小**：`aligned_w * 16 * 1.5 * 2`
  - 1920x1080: `1920 * 16 * 1.5 * 2 = 92,160 字节 ≈ 90KB`
- **分配**：hsram 64 字节对齐（优先），psram 回退
- **生命周期**：create 时分配，destroy 时释放
- **访问者**：VCDEC 硬件写入，GPU 通过地址映射读取

### 6.2 GPU 输出帧

- **大小**：取决于 compress 模式
  - compress=true: `(dst_w / 4) * dst_h * bpp`
  - compress=false: `dst_w * dst_h * bpp`
- **分配**：由 `malloc_cb` 分配（调用者提供，通常用 `bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size)`）
- **释放**：由 `frame_display_cb` 接收方负责（如 DPU flush 后释放）
- **所有权转移**：GPU ctlr 分配 → `frame_display` 回调交出 → 调用者持有 → 调用者释放

### 6.3 JPEG 码流

- **分配/释放**：完全由调用者管理
- **生命周期约束**：码流指针在 `bk_jpeg_decode_frame` 返回前必须有效
- **零拷贝**：pipeline 不复制码流，直接传给 VCDEC

---

## 7. 与现有代码的对比

| 维度 | 旧 `decode` 组件 | 新 `jpeg_stream_pipeline` |
|------|-------------------|---------------------------|
| 解码 API | `h264_decoder_api.h`（旧） | `bk_jpeg_decode_ctlr`（现代） |
| GPU 调用 | 手写 `vg_lite_blit` 循环 | `bk_gpu_ctlr`（SDK strip 并行） |
| strip 处理 | memcpy 到线性帧（~3MB），再全帧 GPU | bond 直连，strip 级并行，零中间拷贝 |
| 旋转支持 | 全帧解完后 GPU 旋转（串行） | GPU 逐 strip 旋转 + DMA（与解码并行） |
| 帧间流水 | 无（同一线程串行） | 天然支持（GPU 处理当前帧 strip N 时 VCDEC 在解 strip N+1） |
| LVGL 互斥 | 每帧调 `lvgl_app_stop_for_casting` | 调用者在 pipeline start 前处理一次即可 |

---

## 8. 待确认项

| 编号 | 项目 | 说明 |
|------|------|------|
| T1 | `bk_flexa_mjpegd_gpu_bond_start` 头文件 | 需搜索确认公开头文件路径，可能为 `<bk_flexa_bond.h>` |
| T2 | `hsram_malloc` 头文件 | 推测为 `<components/bk_hardware_ram.h>`，需确认 |
| T3 | `bk_jpeg_decode_flexa_config_t` 完整字段 | 需阅读 `bk_jpeg_decode_types.h` 确认 |
| T4 | `bk_jpeg_decode_input_t` 字段 | 需确认 `stream`/`stream_len`/`out_buffer` 等字段名 |
| T5 | `gpu_config.tess_width/height` | doorbell 中未设置（保持 0），需确认是否需要 |
| T6 | 解码错误传递 | bond 中 `decode_error` 回调当前为空实现，需确认错误处理策略 |
| T7 | `avdk_err_t` 错误码 | 确认 `AVDK_ERR_BUSY` / `AVDK_ERR_INVALID_STATE` 是否存在 |
| T8 | CMakeLists.txt REQUIRES | 确认 `bk_decode` / `media_service` 等组件名是否正确 |

---

## 9. 典型调用示例

```c
/* 创建 pipeline */
jpeg_stream_pipeline_config_t cfg = {
    .src_width  = 1920,
    .src_height = 1080,
    .dst_width  = 1080,
    .dst_height = 1920,
    .rotate_degree = 90,
    .dst_format = BK_PIXEL_FORMAT_ARGB8888,
    .compress   = true,
    .scale      = false,
    .malloc_cb  = my_frame_malloc,
    .frame_display_cb = my_frame_display,
    .user_data  = &my_context,
};

jpeg_stream_pipeline_handle_t pipeline;
jpeg_stream_pipeline_create(&pipeline, &cfg);
jpeg_stream_pipeline_start(pipeline);

/* 推帧（从网络接收后） */
jpeg_stream_pipeline_push_frame(pipeline, jpeg_buf, jpeg_len, NULL);

/* 停止销毁 */
jpeg_stream_pipeline_stop(pipeline);
jpeg_stream_pipeline_destroy(pipeline);
```
