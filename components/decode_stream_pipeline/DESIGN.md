# decode_stream_pipeline 组件设计文档

## 1. 概述

`decode_stream_pipeline` 是一个编解码无关的高层组件，封装「压缩视频帧输入 → VCDEC FLEXA 解码 → GPU strip 级并行处理 → 输出帧回调」完整流水线。当前支持 JPEG 与 H264 两种编解码后端。

### 1.1 核心思想：编解码后端化

管线核心（帧队列、解码任务、GPU 控制器、FLEXA ring buffer）不含任何编解码分支；每个编解码实现为一个 `dsp_codec_ops_t` 后端表，挂接：

| 钩子 | 职责 |
|------|------|
| `decoder_setup / decoder_teardown` | 硬件解码器生命周期（幂等，支持惰性创建） |
| `bond_start / bond_stop` | 该编解码与 GPU 之间的 FLEXA bond 接拆 |
| `decode_frame` | 提交一帧解码（decode task，持 codec_lock） |
| `admit_frame` | 生产端准入策略（JPEG latest-wins / H264 GOP-aware） |
| `on_activate` | 后端激活策略（H264：等待首个 IDR） |
| `on_push_full` | 队列满时的丢帧策略 |
| `parse_meta` | 码流分类（SOF 解析 / Annex-B NALU 扫描） |
| `diag_frame` | 每帧诊断日志 |

上下文同一时刻只有一个活跃后端 + 一个活跃 FLEXA bond。

## 2. 组件结构

```
decode_stream_pipeline/
├── include/
│   └── decode_stream_pipeline.h          # 公开接口（生命周期 API + 配置 + 回调）
├── src/
│   ├── decode_stream_pipeline.c          # 核心：队列/decode task/GPU/ring/后端切换
│   ├── decode_stream_pipeline_internal.h # 后端接口 + ctx + 共享服务（组件内部）
│   ├── decode_stream_pipeline_jpeg.c     # JPEG 后端（VCDEC JPEG + mjpegd bond + SOF 解析）
│   └── decode_stream_pipeline_h264.c     # H264 后端（VCDEC H264 + h264d bond + NALU 分类）
├── CMakeLists.txt
└── DESIGN.md
```

核心文件不含任何 `bk_jpeg_*` / `bk_h264_*` 直接调用；两个编解码各自的 SDK 头文件只出现在对应后端文件（及内部头文件的 state 定义）中。

## 3. 公开接口

```c
typedef struct decode_stream_pipeline_ctx *decode_stream_pipeline_handle_t;

/* 初始编解码类型：决定 create() 时急切拉起哪个解码器 */
typedef enum {
    DECODE_STREAM_TYPE_AUTO = 0,  /* 默认按 JPEG 初始化，按帧自动切换 */
    DECODE_STREAM_TYPE_JPEG,      /* JPEG 为主，H264 惰性创建 */
    DECODE_STREAM_TYPE_H264,      /* H264 为主，JPEG 惰性创建 */
} decode_stream_type_t;

avdk_err_t decode_stream_pipeline_create(handle_t *, const config_t *);
avdk_err_t decode_stream_pipeline_start(handle_t);
avdk_err_t decode_stream_pipeline_push_frame(handle_t, const uint8_t *stream, uint32_t len, void *owner);
avdk_err_t decode_stream_pipeline_stop(handle_t);
avdk_err_t decode_stream_pipeline_destroy(handle_t);
```

配置在原 `jpeg_stream_pipeline_config_t` 基础上新增 `decode_type` 字段，其余字段（分辨率/GPU 参数/FLEXA 参数/回调/任务参数）保持不变。回调前缀由 `jsp_` 改为 `dsp_`。

## 4. 关键机制

### 4.1 初始后端选择 + 惰性动态切换

- `create()` 按 `decode_type` 选择初始后端并**急切**创建其解码器（AUTO → JPEG）；另一编解码的解码器在**首次收到该类帧**时才惰性创建（`decoder_setup` 幂等）。
- 每次 `push_frame` 重新分类帧：先匹配活跃后端，失败再尝试另一后端；码流类型变化即触发双向切换（JPEG ↔ H264）。
- 切换流程（持 `codec_lock`）：drain 队列 → 拆旧 bond → 惰性建新解码器 → 建新 bond → `on_activate`。失败自动回退旧 bond。
- 纯 JPEG 会话（boot_avi_play）永不分配 H264 解码器内存（`MEM_SLAB_HEAP_UNCODED`）。

### 4.2 codec_lock（解码 / 切换互斥）

decode task 在 `decode_frame` 期间持有 `codec_lock`；切换在持锁下拆 bond。因此切换永远不会在旧解码器解码到一半时拆除其 bond（生产者最多等待一帧的解码时长）。

### 4.3 准入策略（按后端）

| 后端 | 策略 |
|------|------|
| JPEG | 帧独立：每次 push 前 drain 队列（latest-wins，低延迟）；丢弃量聚合上报 |
| H264 | GOP-aware（`DSP_H264_GOP_DROP_ENABLE`）：IDR 重同步并 drain；断链后丢到下个 IDR；队列满同样断链；`on_activate` 时等待首个 IDR（中途入流安全） |

队列深度按 H264 最坏情况（≥4）一次性分配；JPEG 靠 push 前 drain 保证额外槽位不积压旧帧。

### 4.4 会话语义

`start()` 每次重跑 `on_activate`（H264 会话重新等待 IDR）；`stop()` 排空队列并以 `AVDK_ERR_SHUTDOWN` 通知 owner；`destroy()` 经两张 ops 表拆除两个解码器（未用过的为 no-op）。

## 5. 数据流

```
push_frame(stream, len, owner)
    │ 分类 → (必要时)切换后端 → 准入 → 入队(携带后端快照)
    ▼
decode task: pop → [codec_lock] backend decode_frame
    ▼
VCDEC FLEXA 逐 strip 写 ring → bond 自动通知 GPU
    ▼
GPU strip 级并行处理（旋转/压缩/缩放）→ frame_display 回调
```

## 6. 内存管理

- **FLEXA ring**：create 时 PSRAM 64 字节对齐分配（`bk_image_size_get`，NV12），destroy 释放。
- **GPU 输出帧**：`malloc_cb` 分配，所有权随 `frame_display_cb` 移交给调用者。
- **压缩码流**：调用者所有，`frame_consumed_cb`（decode task 或生产者线程）通知释放；drain 丢帧以 `DSP_STATUS_FRAME_DROPPED` 通知。
- **解码器**：按需创建（初始后端急切 + 另一后端惰性），destroy 统一释放。

## 7. 与旧 jpeg_stream_pipeline 的差异

| 维度 | 旧 | 新 |
|------|----|----|
| 命名 | `jpeg_stream_pipeline_*` / `jsp_` | `decode_stream_pipeline_*` / `dsp_` |
| 文件 | 单文件混住两类编解码逻辑 | 核心 + 内部头 + 2 个后端文件 |
| 初始解码器 | 无条件同时创建 JPEG + H264 | `decode_type` 选择急切创建，另一个惰性 |
| 切换 | 单向（H264 后回不去） | 双向，按帧分类自动切换 |
| 切换/解码并发 | 无保护 | `codec_lock` 互斥 |
| 准入/诊断 | 混在 push_frame ~150 行内 | 后端钩子（admit/on_push_full/diag） |

## 8. 待办 / 后续优化

| 编号 | 项目 | 说明 |
|------|------|------|
| T1 | dashcam H264 管线整合 | `dashcam_h264_flexa_decoder.c` 存在重复的 H264 解码+bond 逻辑，可迁移到本组件 |
| T2 | GOP drop 编译开关 Kconfig 化 | `DSP_H264_GOP_DROP_ENABLE` 目前是 .c 内宏，可改为 Kconfig |
| T3 | 统计接口 | 暴露 per-codec in/out/drop 计数便于现场诊断 |
| T4 | AUTO 全惰性 | 若 create 期内存紧张，可将 AUTO 改为首个帧分类后再拉起解码器 |
