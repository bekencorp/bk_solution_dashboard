#include "dashcam_app.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "components/log.h"
#include "dashcam_camera.h"
#include "dashcam_config.h"
#include "dashcam_recorder.h"
#include "dashcam_storage.h"
#include "dashcam_video.h"
#include "os/os.h"

#define TAG "d_app"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define DASHCAM_APP_TICK_MS 1000
#define DASHCAM_TICK_PRIO  4
#define DASHCAM_TICK_STACK 6144
#define DASHCAM_PLAYBACK_STOP_TIMEOUT_MS 3000U

/*
 * Recording and playback are independent (they can both be active at once), so
 * each has its own state variable. s_rec is owned by the recorder path (start
 * capture / rotation worker / stop), s_play by the playback path (play /
 * stop_playback); neither transition touches the other's variable.
 */
static dashcam_rec_state_t  s_rec = DASHCAM_REC_IDLE;
static dashcam_play_state_t s_play = DASHCAM_PLAY_IDLE;
static lv_obj_t *s_parent = NULL;
static uint32_t s_segment_start_ms = 0;
static char s_status[64] = "IDLE";
static char s_pending_play_path[DASHCAM_STORAGE_MAX_PATH];

/*
 * Segment-rotation tick.
 *
 * An rtos timer wakes a dedicated worker once per second; the worker does the
 * heavy rotation (recorder stop -> MP4 close/open). Driving it from an rtos timer
 * instead of an lv_timer keeps segment rotation running while the LVGL task is
 * stopped for casting / assist view (recording keeps going in the background).
 *
 * The worker runs in its own task, but needs no lock against the UI dashcam ops:
 * it only touches the recorder (camera -> H264 -> MP4), which is independent of
 * the player (H264 -> GPU) that the UI ops drive. Recording state (s_rec) is
 * orthogonal to playback, so rotation gates purely on s_rec == RECORDING and
 * keeps running while a clip plays back. UI ops only bring the recorder up/down
 * while s_rec != RECORDING (i.e. when the worker is idle); the one op that tears
 * the recorder down while it may be recording is shutdown(), which joins the
 * worker (stop_tick) first. The shared scalars (s_rec, s_play, s_segment_start_ms)
 * are word-atomic on this target.
 */
static beken_timer_t     s_tick_timer;
static beken_thread_t    s_tick_thread = NULL;
static beken_semaphore_t s_tick_wake = NULL;
static beken_semaphore_t s_tick_exit = NULL;
static volatile bool     s_tick_running = false;

static void dashcam_app_set_status(const char *text)
{
    snprintf(s_status, sizeof(s_status), "%s", text != NULL ? text : "");
    LOGD("status=%s\n", s_status);
}

/*
 * Derive the single status-line string from the two orthogonal states. Playback
 * owns the display while a clip is playing, so it takes priority; otherwise the
 * line reflects the background recording state.
 */
static void dashcam_app_refresh_status(void)
{
    if (s_play == DASHCAM_PLAY_PLAYING)
    {
        dashcam_app_set_status("PLAYBACK");
    }
    else if (s_rec == DASHCAM_REC_RECORDING)
    {
        dashcam_app_set_status("REC");
    }
    else if (s_rec == DASHCAM_REC_STOPPED)
    {
        dashcam_app_set_status("REC FULL");
    }
    else
    {
        dashcam_app_set_status("IDLE");
    }
}

static bool dashcam_app_record_allowed(void)
{
#if DASHCAM_RELEASE_BUILD
    /* Release: always allowed; ring-recycle keeps the card from filling. */
    return true;
#else
    uint32_t count = 0;
    if (dashcam_storage_count(&count) != BK_OK)
    {
        return false;
    }
    LOGI("dev build, dashcam_app_record_allowed: count=%u, max=%u\n", count, DASHCAM_DEV_MAX_FILES);
    return count < DASHCAM_DEV_MAX_FILES;
#endif
}

static bk_err_t dashcam_app_begin_segment(void)
{
    char path[DASHCAM_STORAGE_MAX_PATH];

    LOGD("begin_segment\n");

#if DASHCAM_RELEASE_BUILD
    if (dashcam_storage_ensure_record_space() != BK_OK)
    {
        LOGE("insufficient SD space after adaptive cleanup\n");
        return BK_FAIL;
    }
#endif

    if (dashcam_storage_make_record_path(path, sizeof(path)) != BK_OK)
    {
        LOGE("make record path failed\n");
        return BK_FAIL;
    }

    if (dashcam_recorder_start(path) != BK_OK)
    {
        LOGE("recorder start failed (path=%s)\n", path);
        return BK_FAIL;
    }

    s_segment_start_ms = rtos_get_time();
    /* Don't clobber the "PLAYBACK" label when rotation happens under a clip. */
    if (s_play == DASHCAM_PLAY_IDLE)
    {
        dashcam_app_set_status("REC");
    }
    LOGI("begin_segment: recording to %s\n", path);
    return BK_OK;
}

/*
 * Recording is not active (dev file cap reached, or a record open failed). Tear
 * the encoder/camera down so no H264 frames pile up with no consumer. There is
 * no preview to keep the camera up for, so power it down entirely.
 */
static void dashcam_app_stop_recording(void)
{
    LOGI("stop_recording: tearing down recorder\n");

    /* Only the recorder path is touched here; the playback surface (if any) is
     * independent and left alone. This can run from the rotation worker while a
     * clip is playing (s_play == PLAYING), in which case the status line stays
     * "PLAYBACK" and only flips to "REC FULL" once playback ends. */
    dashcam_camera_close();
    s_rec = DASHCAM_REC_STOPPED;

    if (s_play == DASHCAM_PLAY_IDLE)
    {
        dashcam_app_set_status("REC FULL");
    }
    LOGI("recording disabled, rec=%d\n", (int)s_rec);
}

/*
 * Bring up the continuous recording session (camera MP/flexa->H264 encoder) and
 * the first segment. On the dev file cap, or a failed record open, fall back via
 * stop_recording. Idempotent while already recording.
 */
static void dashcam_app_start_capture(void)
{
    if (s_rec == DASHCAM_REC_RECORDING)
    {
        LOGI("start_capture: already RECORDING, skip\n");
        return;
    }

    bool allowed = dashcam_app_record_allowed();
    LOGI("start_capture: record_allowed=%d viewer=%p\n",
         (int)allowed, (void *)s_parent);

    if (allowed)
    {
        if (dashcam_camera_open() == BK_OK)
        {
            if (dashcam_app_begin_segment() == BK_OK)
            {
                s_rec = DASHCAM_REC_RECORDING;
                LOGI("start_capture: recording ON (rec=RECORDING)\n");
                return;
            }
            /* Don't leave the encoder bonded with no recorder draining it. */
            LOGE("start_capture: begin_segment failed, closing camera\n");
            dashcam_camera_close();
        }
        else
        {
            LOGE("start_capture: record camera open failed\n");
        }
    }

    LOGI("start_capture: recording NOT started (allowed=%d), stopping recorder\n",
         (int)allowed);
    dashcam_app_stop_recording();
}

static void dashcam_app_rotate_segment(void)
{
    char completed_path[DASHCAM_STORAGE_MAX_PATH];
    const char *current_path = dashcam_recorder_current_path();
    bk_err_t stop_ret;
    dashcam_mp4_check_result_t check_result;

    snprintf(completed_path, sizeof(completed_path), "%s",
             current_path != NULL ? current_path : "");

    LOGI("rotate_segment: closing current MP4, starting next\n");
    stop_ret = dashcam_recorder_stop();
    if (stop_ret != BK_OK)
    {
        LOGW("rotate_segment: recorder stop failed for %s\n", completed_path);
    }

    /*
     * This function runs only on dcam_tick, not the LVGL task. Validate only
     * after a successful stop. Delete empty or structurally invalid clips, but
     * retain transient SD I/O errors because their contents are still unknown.
     */
    if (stop_ret == BK_OK && completed_path[0] != '\0')
    {
        check_result = dashcam_storage_check_mp4(completed_path);
        if (check_result == DASHCAM_MP4_CHECK_EMPTY ||
            check_result == DASHCAM_MP4_CHECK_INVALID)
        {
            LOGW("rotate_segment: removing unplayable MP4: %s check=%d\n",
                 completed_path, (int)check_result);
            if (dashcam_storage_delete_record(completed_path) != BK_OK)
            {
                LOGE("rotate_segment: failed to remove unplayable MP4: %s\n",
                     completed_path);
            }
        }
        else if (check_result != DASHCAM_MP4_CHECK_VALID)
        {
            LOGW("rotate_segment: retaining unchecked MP4: %s check=%d\n",
                 completed_path, (int)check_result);
        }
    }

    if (!dashcam_app_record_allowed())
    {
        dashcam_app_stop_recording();
        return;
    }

    if (dashcam_app_begin_segment() != BK_OK)
    {
        dashcam_app_stop_recording();
    }
}

/*
 * rtos-timer callback (runs in the timer-daemon task): the rotation does blocking
 * file IO (MP4 close/open) and would stall every other software timer, so here we
 * only wake the worker and let it do the heavy lifting.
 */
static void dashcam_app_tick_timer_cb(void *arg)
{
    (void)arg;
    if (s_tick_wake != NULL)
    {
        rtos_set_semaphore(&s_tick_wake);
    }
}

/*
 * Segment-rotation worker. Runs in its own task (independent of the LVGL task), so
 * rotation keeps going while LVGL is stopped for casting / assist view. It only
 * touches the recorder, which no UI op touches while recording (see the note on
 * the tick state above), so it needs no lock.
 */
static void dashcam_app_tick_thread(void *arg)
{
    (void)arg;

    while (s_tick_running)
    {
        if (rtos_get_semaphore(&s_tick_wake, BEKEN_WAIT_FOREVER) != BK_OK)
        {
            continue;
        }
        if (!s_tick_running)
        {
            break;
        }

        if (s_tick_running &&
            s_rec == DASHCAM_REC_RECORDING &&
            (rtos_get_time() - s_segment_start_ms) >= (DASHCAM_SEGMENT_SECONDS * 1000u))
        {
            dashcam_app_rotate_segment();
        }
    }

    if (s_tick_exit != NULL)
    {
        rtos_set_semaphore(&s_tick_exit);
    }
    s_tick_thread = NULL;
    rtos_delete_thread(NULL);
}

/* Start the rotation tick (rtos timer + worker). Idempotent. */
static void dashcam_app_start_tick(void)
{
    if (s_tick_running)
    {
        return;
    }

    if (rtos_init_semaphore(&s_tick_wake, 1) != BK_OK)
    {
        s_tick_wake = NULL;
        LOGE("tick: wake sem init failed\n");
        return;
    }
    if (rtos_init_semaphore(&s_tick_exit, 1) != BK_OK)
    {
        rtos_deinit_semaphore(&s_tick_wake);
        s_tick_wake = NULL;
        s_tick_exit = NULL;
        LOGE("tick: exit sem init failed\n");
        return;
    }

    s_tick_running = true;
    if (rtos_create_thread(&s_tick_thread, DASHCAM_TICK_PRIO, "dcam_tick",
                           (beken_thread_function_t)dashcam_app_tick_thread,
                           DASHCAM_TICK_STACK, NULL) != BK_OK)
    {
        s_tick_running = false;
        rtos_deinit_semaphore(&s_tick_wake);
        rtos_deinit_semaphore(&s_tick_exit);
        s_tick_wake = NULL;
        s_tick_exit = NULL;
        s_tick_thread = NULL;
        LOGE("tick: worker thread create failed\n");
        return;
    }

    if (rtos_init_timer(&s_tick_timer, DASHCAM_APP_TICK_MS, dashcam_app_tick_timer_cb, NULL) == BK_OK)
    {
        rtos_start_timer(&s_tick_timer);
    }
    else
    {
        LOGE("tick: timer init failed\n");
    }
    LOGI("rotation tick started\n");
}

/* Stop the rotation tick and join the worker, so no rotation is in flight once
 * this returns (used by shutdown before it tears the recorder down). */
static void dashcam_app_stop_tick(void)
{
    if (!s_tick_running)
    {
        return;
    }

    if (rtos_is_timer_init(&s_tick_timer))
    {
        rtos_stop_timer(&s_tick_timer);
        rtos_deinit_timer(&s_tick_timer);
    }

    s_tick_running = false;
    if (s_tick_wake != NULL)
    {
        rtos_set_semaphore(&s_tick_wake);
    }
    if (s_tick_exit != NULL)
    {
        rtos_get_semaphore(&s_tick_exit, BEKEN_WAIT_FOREVER);
    }

    if (s_tick_wake != NULL)
    {
        rtos_deinit_semaphore(&s_tick_wake);
        s_tick_wake = NULL;
    }
    if (s_tick_exit != NULL)
    {
        rtos_deinit_semaphore(&s_tick_exit);
        s_tick_exit = NULL;
    }
    LOGI("rotation tick stopped\n");
}

/*
 * Casting / assist-view hooks. The rotation tick is now rtos-driven and runs
 * independently of the LVGL task, so it keeps rotating while casting; there is
 * nothing to pause. Resume is kept as an idempotent safety net that re-arms the
 * tick if recording is active.
 */
void dashcam_app_pause_segment_tick(void)
{
    /* no-op: rtos tick keeps running while LVGL is stopped */
}

void dashcam_app_resume_segment_tick(void)
{
    if (s_rec == DASHCAM_REC_RECORDING)
    {
        dashcam_app_start_tick();
    }
}

void dashcam_app_boot_start(void)
{
    LOGD("boot_start (rec=%d)\n", (int)s_rec);

    if (s_rec != DASHCAM_REC_IDLE)
    {
        return;
    }

    (void)dashcam_storage_init();
    /* Must run before camera/recorder open: continuous recording never idles. */
    (void)dashcam_storage_boot_reclaim();
    dashcam_app_start_capture();
    dashcam_app_start_tick();
    LOGI("continuous recording started, rec=%d\n", (int)s_rec);
}

bk_err_t dashcam_app_record_start(void)
{
    LOGD("record_start (rec=%d play=%d)\n", (int)s_rec, (int)s_play);

    if (s_rec == DASHCAM_REC_RECORDING)
    {
        LOGI("record_start: already recording\n");
        return BK_OK;
    }

    (void)dashcam_storage_init();
    (void)dashcam_storage_boot_reclaim();
    dashcam_app_start_capture();
    dashcam_app_start_tick();

    if (s_rec != DASHCAM_REC_RECORDING)
    {
        /* Blocked (e.g. dev file cap still reached, or camera/record open failed);
         * start_capture already settled into STOPPED and logged the reason. */
        LOGW("record_start: could not start, rec=%d\n", (int)s_rec);
        return BK_FAIL;
    }

    LOGI("record_start: recording ON\n");
    return BK_OK;
}

void dashcam_app_record_stop(void)
{
    LOGD("record_stop (rec=%d play=%d)\n", (int)s_rec, (int)s_play);

    if (s_rec != DASHCAM_REC_RECORDING)
    {
        LOGI("record_stop: not recording, rec=%d\n", (int)s_rec);
        return;
    }

    /* Join the rotation worker before touching the recorder while it may be
     * actively recording (same ordering shutdown() uses), then finalize the
     * current MP4 and power the camera down. Playback, if any, is on independent
     * hardware and left running. */
    dashcam_app_stop_tick();
    (void)dashcam_recorder_stop();
    dashcam_camera_close();
    s_rec = DASHCAM_REC_STOPPED;

    if (s_play == DASHCAM_PLAY_IDLE)
    {
        dashcam_app_set_status("REC OFF");
    }
    LOGI("record_stop: recording OFF\n");
}

void dashcam_app_attach(lv_obj_t *preview_parent)
{
    LOGD("attach parent=%p (rec=%d play=%d)\n",
         (void *)preview_parent, (int)s_rec, (int)s_play);

    if (preview_parent == NULL || !lv_obj_is_valid(preview_parent))
    {
        LOGW("attach: invalid parent\n");
        return;
    }

    s_parent = preview_parent;
    (void)dashcam_storage_init();

    /* Recording normally runs headless from boot; cover the page being entered
     * before boot_start ran, or after a full shutdown. There is no live camera
     * preview to show (playback-only video surface) - recording just keeps
     * running in the background and the page is used for clip playback. */
    if (s_rec == DASHCAM_REC_IDLE)
    {
        (void)dashcam_storage_boot_reclaim();
        dashcam_app_start_capture();
        dashcam_app_start_tick();
    }

    LOGI("page attached, rec=%d\n", (int)s_rec);
}

void dashcam_app_detach(void)
{
    LOGD("detach (rec=%d play=%d)\n", (int)s_rec, (int)s_play);

    /* The DPU worker owns player/GPU/LVGL teardown. This thread only cancels the
     * pending completion callback and queues STOP. Background recording remains
     * untouched (power-on continuous recording, req5 §9.1). */
    if (s_play == DASHCAM_PLAY_PLAYING)
    {
        dashcam_video_set_ready_callback(NULL, NULL);
        s_play = DASHCAM_PLAY_IDLE;
    }
    (void)dashcam_video_stop_sync(DASHCAM_PLAYBACK_STOP_TIMEOUT_MS);
    s_parent = NULL;

    dashcam_app_refresh_status();
    LOGI("page detached, recording continues, rec=%d\n", (int)s_rec);
}

void dashcam_app_shutdown(void)
{
    LOGD("shutdown (rec=%d play=%d)\n", (int)s_rec, (int)s_play);

    /* Join the worker first so no segment rotation is in flight while we tear the
     * recorder down (the only place a UI op touches the recorder while it may be
     * actively recording). */
    dashcam_app_stop_tick();

    /* Worker joined above, so no rotation is in flight: fully tear the recorder
     * down here (destroys the worker thread + semaphores). Rotation/record_stop
     * only pause via dashcam_recorder_stop(), never close/delete. */
    dashcam_recorder_destroy();
    dashcam_video_set_ready_callback(NULL, NULL);
    /* The playback worker closes decoder/GPU before it restores LVGL. */
    (void)dashcam_video_stop_sync(DASHCAM_PLAYBACK_STOP_TIMEOUT_MS);
    dashcam_camera_close();
    s_parent = NULL;
    s_rec = DASHCAM_REC_IDLE;
    s_play = DASHCAM_PLAY_IDLE;
    dashcam_app_set_status("IDLE");
    LOGI("shutdown done\n");
}

static void dashcam_app_play_when_sink_ready(bk_err_t result, void *user_data)
{
    (void)user_data;

    if (s_play != DASHCAM_PLAY_PLAYING)
    {
        return;
    }

    if (result != BK_OK)
    {
        LOGE("player play failed\n");
        s_play = DASHCAM_PLAY_IDLE;
        dashcam_app_refresh_status();
        return;
    }

    LOGI("playback ready (rec=%d)\n", (int)s_rec);
}

bk_err_t dashcam_app_play(const char *path)
{
    LOGD("play req: %s\n", path != NULL ? path : "(null)");

    if (path == NULL || path[0] == '\0' || s_parent == NULL)
    {
        LOGW("play: bad param (parent=%p)\n", (void *)s_parent);
        return BK_ERR_PARAM;
    }

    /*
     * In-session clip switch (fast path).
     *
     * Keep LVGL released and the sink alive for the whole session. File switches
     * are queued to the same worker that owns player/GPU setup and teardown, so
     * VG-Lite ownership never moves concurrently across application threads.
     */
    if (s_play == DASHCAM_PLAY_PLAYING)
    {
        snprintf(s_pending_play_path, sizeof(s_pending_play_path), "%s", path);
        if (dashcam_video_switch_file(path) != BK_OK)
        {
            LOGE("in-session switch play failed: %s\n", path);
            dashcam_app_stop_playback();
            return BK_FAIL;
        }
        dashcam_app_set_status("PLAYBACK");
        LOGI("in-session switch: %s (rec=%d)\n", path, (int)s_rec);
        return BK_OK;
    }

    snprintf(s_pending_play_path, sizeof(s_pending_play_path), "%s", path);
    s_play = DASHCAM_PLAY_PLAYING;
    dashcam_app_set_status("PLAYBACK");
    dashcam_video_set_ready_callback(dashcam_app_play_when_sink_ready,
                                     NULL);

    if (dashcam_video_start_sink(s_parent, s_pending_play_path) != BK_OK)
    {
        LOGE("playback sink start failed\n");
        dashcam_video_set_ready_callback(NULL, NULL);
        s_play = DASHCAM_PLAY_IDLE;
        dashcam_app_refresh_status();
        return BK_FAIL;
    }

    return BK_OK;
}

void dashcam_app_stop_playback(void)
{
    LOGD("stop_playback (rec=%d play=%d)\n", (int)s_rec, (int)s_play);

    /* The playback worker serializes player/GPU close and LVGL restore. */
    dashcam_video_set_ready_callback(NULL, NULL);
    s_play = DASHCAM_PLAY_IDLE;
    (void)dashcam_video_stop_sync(DASHCAM_PLAYBACK_STOP_TIMEOUT_MS);

    dashcam_app_refresh_status();
    LOGI("stop_playback: playback off, rec=%d\n", (int)s_rec);
}

dashcam_rec_state_t dashcam_app_rec_state(void)
{
    return s_rec;
}

bool dashcam_app_is_playing(void)
{
    return s_play == DASHCAM_PLAY_PLAYING;
}

const char *dashcam_app_status_text(void)
{
    return s_status;
}
