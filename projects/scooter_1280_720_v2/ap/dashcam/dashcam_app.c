#include "dashcam_app.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "components/log.h"
#include "dashcam_camera.h"
#include "dashcam_config.h"
#include "dashcam_player.h"
#include "dashcam_recorder.h"
#include "dashcam_storage.h"
#include "dashcam_video.h"
#include "os/os.h"

#define TAG "dashcam_app"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define DASHCAM_APP_TICK_MS 1000

static dashcam_app_state_t s_state = DASHCAM_APP_IDLE;
static lv_obj_t *s_parent = NULL;
static lv_timer_t *s_tick_timer = NULL;
static uint32_t s_segment_start_ms = 0;
static char s_status[64] = "IDLE";

static void dashcam_app_set_status(const char *text)
{
    snprintf(s_status, sizeof(s_status), "%s", text != NULL ? text : "");
    LOGD("status=%s\n", s_status);
}

/*
 * Pick the camera mode for a recording session: full RECORD (with the SP live
 * preview channel) when a page is showing the preview, or the lighter
 * RECORD_ONLY (no SP buffers) for headless/background recording. Keeping SP off
 * when nobody is viewing is what keeps the AP heap from being starved at boot
 * (req5 §9.1 OOM fix).
 */
static dashcam_camera_mode_t dashcam_app_record_camera_mode(void)
{
    return (s_parent != NULL) ? DASHCAM_CAMERA_MODE_RECORD
                              : DASHCAM_CAMERA_MODE_RECORD_ONLY;
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
    return count < DASHCAM_DEV_MAX_FILES;
#endif
}

static bk_err_t dashcam_app_begin_segment(void)
{
    char path[DASHCAM_STORAGE_MAX_PATH];

    LOGD("begin_segment\n");

#if DASHCAM_RELEASE_BUILD
    (void)dashcam_storage_recycle_for_release();
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
    dashcam_app_set_status("REC");
    LOGI("begin_segment: recording to %s\n", path);
    return BK_OK;
}

/*
 * Recording is not active (dev file cap reached, or a record-mode open failed).
 * Tear the encoder down so no H264 frames pile up with no consumer. Keep a
 * preview-only camera up only while the page is actively showing it; otherwise
 * power the camera down entirely (no viewer, no recording).
 */
static void dashcam_app_switch_to_live_only(void)
{
    bool had_preview = (s_parent != NULL);

    LOGI("switch_to_live_only: tearing down recorder (had_preview=%d)\n",
         (int)had_preview);

    if (had_preview)
    {
        dashcam_video_stop();
    }
    dashcam_camera_close();

    if (had_preview)
    {
        if (dashcam_camera_open(DASHCAM_CAMERA_MODE_PREVIEW) != BK_OK)
        {
            LOGE("preview-only camera open failed\n");
            dashcam_app_set_status("CAM FAIL");
            s_state = DASHCAM_APP_IDLE;
            return;
        }
        (void)dashcam_video_start(s_parent);
        dashcam_app_set_status("LIVE (REC FULL)");
    }
    else
    {
        dashcam_app_set_status("REC FULL");
    }

    s_state = DASHCAM_APP_LIVE_ONLY;
    LOGI("recording disabled (dev file cap), state=%d\n", (int)s_state);
}

/*
 * Bring up the continuous recording session: ISP RECORD mode (MP/flexa->H264
 * plus the SP channel for the live view) and the first segment. On the dev file
 * cap, or a failed record open, fall back via switch_to_live_only. Idempotent
 * while already recording.
 */
static void dashcam_app_start_capture(void)
{
    if (s_state == DASHCAM_APP_LIVE_REC)
    {
        LOGI("start_capture: already LIVE_REC, skip\n");
        return;
    }

    bool allowed = dashcam_app_record_allowed();
    dashcam_camera_mode_t mode = dashcam_app_record_camera_mode();
    LOGI("start_capture: record_allowed=%d viewer=%p -> camera mode=%d\n",
         (int)allowed, (void *)s_parent, (int)mode);

    if (allowed)
    {
        if (dashcam_camera_open(mode) == BK_OK)
        {
            if (dashcam_app_begin_segment() == BK_OK)
            {
                s_state = DASHCAM_APP_LIVE_REC;
                LOGI("start_capture: recording ON (camera mode=%d, state=LIVE_REC)\n",
                     (int)mode);
                return;
            }
            /* Don't leave the encoder bonded with no recorder draining it. */
            LOGE("start_capture: begin_segment failed, closing camera\n");
            dashcam_camera_close();
        }
        else
        {
            LOGE("start_capture: record camera open failed (mode=%d)\n", (int)mode);
        }
    }

    LOGI("start_capture: recording NOT started (allowed=%d), fall back to live-only\n",
         (int)allowed);
    dashcam_app_switch_to_live_only();
}

/*
 * Tear the current recording session down and bring it back up in the camera
 * mode that matches the current viewer state (RECORD vs RECORD_ONLY). Used to
 * add/drop the SP preview channel when a page attaches/detaches without leaving
 * the dashcam recording stopped. This starts a new MP4 segment.
 */
static void dashcam_app_restart_capture(void)
{
    LOGI("restart_capture: stop recorder + close camera, then reopen (viewer=%p)\n",
         (void *)s_parent);
    (void)dashcam_recorder_stop();
    dashcam_camera_close();
    s_state = DASHCAM_APP_IDLE;
    dashcam_app_start_capture();
}

static void dashcam_app_rotate_segment(void)
{
    LOGI("rotate_segment: closing current MP4, starting next\n");
    (void)dashcam_recorder_stop();

    if (!dashcam_app_record_allowed())
    {
        dashcam_app_switch_to_live_only();
        return;
    }

    if (dashcam_app_begin_segment() != BK_OK)
    {
        dashcam_app_switch_to_live_only();
    }
}

static void dashcam_app_tick_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_state != DASHCAM_APP_LIVE_REC)
    {
        return;
    }

    if ((rtos_get_time() - s_segment_start_ms) >= (DASHCAM_SEGMENT_SECONDS * 1000u))
    {
        dashcam_app_rotate_segment();
    }
}

static void dashcam_app_start_tick(void)
{
    if (s_tick_timer == NULL)
    {
        s_tick_timer = lv_timer_create(dashcam_app_tick_cb, DASHCAM_APP_TICK_MS, NULL);
    }
}

static void dashcam_app_stop_tick(void)
{
    if (s_tick_timer != NULL)
    {
        lv_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }
}

void dashcam_app_boot_start(void)
{
    LOGD("boot_start (state=%d)\n", (int)s_state);

    if (s_state != DASHCAM_APP_IDLE)
    {
        return;
    }

    (void)dashcam_storage_init();
    dashcam_app_start_capture();
    dashcam_app_start_tick();
    LOGI("continuous recording started, state=%d\n", (int)s_state);
}

void dashcam_app_attach(lv_obj_t *preview_parent)
{
    LOGD("attach parent=%p (state=%d)\n", (void *)preview_parent, (int)s_state);

    if (preview_parent == NULL || !lv_obj_is_valid(preview_parent))
    {
        LOGW("attach: invalid parent\n");
        return;
    }

    s_parent = preview_parent;
    (void)dashcam_storage_init();

    /* Recording normally runs from boot; cover the page being entered before
     * boot_start ran, or after a full shutdown. */
    if (s_state == DASHCAM_APP_IDLE)
    {
        dashcam_app_start_capture();
        dashcam_app_start_tick();
    }
    else if (s_state == DASHCAM_APP_LIVE_REC &&
             dashcam_camera_get_mode() == DASHCAM_CAMERA_MODE_RECORD_ONLY)
    {
        /* Was recording headless (record-only, no SP). A viewer arrived, so
         * restart the session with the SP preview channel up. */
        dashcam_app_restart_capture();
    }
    else if (s_state == DASHCAM_APP_LIVE_ONLY &&
             dashcam_camera_get_mode() == DASHCAM_CAMERA_MODE_OFF)
    {
        /* Dev cap was reached while no one was viewing; bring the preview camera
         * up so the page still shows a live image. */
        if (dashcam_camera_open(DASHCAM_CAMERA_MODE_PREVIEW) == BK_OK)
        {
            dashcam_app_set_status("LIVE (REC FULL)");
        }
    }

    /* Live view needs a readable preview channel; RECORD_ONLY has none. */
    if ((s_state == DASHCAM_APP_LIVE_REC || s_state == DASHCAM_APP_LIVE_ONLY) &&
        dashcam_camera_get_mode() != DASHCAM_CAMERA_MODE_OFF &&
        dashcam_camera_get_mode() != DASHCAM_CAMERA_MODE_RECORD_ONLY)
    {
        (void)dashcam_video_start(s_parent);
    }

    LOGI("preview attached, state=%d\n", (int)s_state);
}

void dashcam_app_detach(void)
{
    LOGD("detach (state=%d)\n", (int)s_state);

    /* Page is leaving: drop the preview but keep recording in the background
     * (power-on continuous recording, req5 §9.1). */
    dashcam_video_stop();

    if (s_state == DASHCAM_APP_PLAYBACK)
    {
        /* Was viewing a clip; return to background recording. */
        (void)dashcam_player_stop();
        s_parent = NULL;
        dashcam_app_start_capture();
        LOGI("detached from playback, state=%d\n", (int)s_state);
        return;
    }

    if (s_state == DASHCAM_APP_LIVE_ONLY && !dashcam_app_record_allowed())
    {
        /* Recording disabled and no viewer left: power the camera down. */
        dashcam_camera_close();
        dashcam_app_set_status("REC FULL");
        s_parent = NULL;
        LOGI("preview detached, recording continues, state=%d\n", (int)s_state);
        return;
    }

    if (s_state == DASHCAM_APP_LIVE_REC &&
        dashcam_camera_get_mode() == DASHCAM_CAMERA_MODE_RECORD)
    {
        /* Leaving the page while recording with the SP preview up: drop back to
         * record-only so the 960x540 SP buffers are freed for the rest of the UI
         * (req5 §9.1 OOM fix). Restarting starts a new segment. */
        s_parent = NULL;
        dashcam_app_restart_capture();
        LOGI("preview detached, record-only continues, state=%d\n", (int)s_state);
        return;
    }

    s_parent = NULL;
    LOGI("preview detached, recording continues, state=%d\n", (int)s_state);
}

void dashcam_app_shutdown(void)
{
    LOGD("shutdown (state=%d)\n", (int)s_state);
    dashcam_app_stop_tick();
    (void)dashcam_recorder_stop();
    (void)dashcam_player_stop();
    dashcam_video_stop();
    dashcam_camera_close();
    s_parent = NULL;
    s_state = DASHCAM_APP_IDLE;
    dashcam_app_set_status("IDLE");
    LOGI("shutdown done\n");
}

bk_err_t dashcam_app_play(const char *path)
{
    LOGD("play req: %s\n", path != NULL ? path : "(null)");

    if (path == NULL || path[0] == '\0' || s_parent == NULL)
    {
        LOGW("play: bad param (parent=%p)\n", (void *)s_parent);
        return BK_ERR_PARAM;
    }

    /* Pause the recording session: playback needs the H264 decoder + canvas and
     * the camera/encoder released. Recording resumes on resume_live / detach.
     * The segment-rotation tick is a no-op outside LIVE_REC, so it is left
     * running. */
    (void)dashcam_player_stop();
    (void)dashcam_recorder_stop();
    dashcam_video_stop();
    dashcam_camera_close();

    if (dashcam_video_start_sink(s_parent) != BK_OK)
    {
        LOGE("playback sink start failed\n");
        dashcam_app_resume_live();
        return BK_FAIL;
    }

    if (dashcam_player_play(path) != BK_OK)
    {
        LOGE("player play failed: %s\n", path);
        dashcam_app_resume_live();
        return BK_FAIL;
    }

    s_state = DASHCAM_APP_PLAYBACK;
    dashcam_app_set_status("PLAYBACK");
    LOGI("playback: %s\n", path);
    return BK_OK;
}

void dashcam_app_resume_live(void)
{
    lv_obj_t *parent = s_parent;

    LOGD("resume_live\n");
    (void)dashcam_player_stop();
    dashcam_video_stop();

    /* Restart the background recording session, then re-show the live preview
     * if the page is still active. */
    dashcam_app_start_capture();

    if (parent != NULL &&
        (s_state == DASHCAM_APP_LIVE_REC || s_state == DASHCAM_APP_LIVE_ONLY) &&
        dashcam_camera_get_mode() != DASHCAM_CAMERA_MODE_OFF)
    {
        (void)dashcam_video_start(parent);
    }
}

dashcam_app_state_t dashcam_app_get_state(void)
{
    return s_state;
}

const char *dashcam_app_status_text(void)
{
    return s_status;
}
