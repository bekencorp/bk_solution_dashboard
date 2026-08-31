#include "music_player_ui.h"

#include <stdio.h>
#include <string.h>

#include <components/log.h>
#include <components/usb.h>
#include <components/usb_types.h>
#include <components/cherryusb/usbh_msc.h>
#include <os/mem.h>
#include <os/os.h>

#include "a2dp_sink_demo.h"
#include "beken_ui.h"
#include "bk_avrcp_ct_service.h"
#include "components/bluetooth/bk_dm_avrcp_types.h"
#include "bk_filesystem.h"
#include "bk_partition.h"
#include "bk_std_header.h"
#include "bk_vfs.h"
#include "components/bk_audio_player/bk_audio_player.h"
#include "components/bk_audio_player/plugins/decoders/bk_audio_player_aac_decoder.h"
#include "components/bk_audio_player/plugins/decoders/bk_audio_player_mp3_decoder.h"
#include "components/bk_audio_player/plugins/decoders/bk_audio_player_wav_decoder.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_aac_metadata_parser.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_mp3_metadata_parser.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_wav_metadata_parser.h"
#include "components/bk_audio_player/plugins/sinks/bk_audio_player_onboard_speaker_sink.h"
#include "components/bk_audio_player/plugins/sources/bk_audio_player_file_source.h"
#include "event_runtime.h"
#include "klok_lvgl_preview.h"
#include "klok_mv_render.h"
#include "klok_player_adapter.h"
#include "lv_vendor.h"
#include "sdcard_mtp.h"
#include "video_play_engine_api.h"

#define TAG "music_player"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define MP_MAX_TRACKS       80
#define MP_PATH_MAX         192
#define MP_TITLE_MAX        256
#define MP_ARTIST_MAX       64
#define MP_POLL_MS          500
#define MP_LYRIC_FX_MS      250
#define MP_TTF_CACHE_GLYPHS 64
#define MP_BACKGROUND_FILE  "/sd0/musicBackground.mp4"
#define MP_BG_RETRY_TICKS   4

typedef struct {
    char path[MP_PATH_MAX];
    char title[MP_TITLE_MAX];
    char artist[MP_ARTIST_MAX];
    uint32_t duration_ms;
} mp_track_t;

typedef enum {
    MP_LOCAL_STOPPED = 0,
    MP_LOCAL_PLAYING,
    MP_LOCAL_PAUSED,
} mp_local_state_t;

typedef struct {
    char title[MP_TITLE_MAX];
    char artist[MP_ARTIST_MAX];
    uint8_t connected;
    uint8_t playing;
} mp_bt_state_t;

static mp_track_t *s_tracks;
static int s_track_count;
static int s_track_index = -1;
static bk_audio_player_handle_t s_player;
static volatile mp_local_state_t s_local_state;
static mp_bt_state_t s_bt;
static lv_timer_t *s_poll_timer;
static lv_timer_t *s_lyric_fx_timer;
static bool s_udisk_mounted;
static bool s_usb_host_open;
static bool s_show_udisk;
static uint8_t s_font_retry_ticks;
static uint8_t s_bg_retry_ticks;
static uint8_t s_lyric_fx_phase;
static uint8_t s_lyric_angle_index = 1U;
static uint8_t s_lyric_angle_sequence;
static bool s_background_enabled;
static volatile bool s_leave_pending;
static char s_displayed_bt_title[MP_TITLE_MAX];

/* A single PSRAM copy is shared by all sizes and retained for process life. */
static void *s_ttf_data;
static size_t s_ttf_size;
static lv_font_t *s_ttf_24;
static lv_font_t *s_ttf_32;
static lv_font_t *s_ttf_82;

static bool mp_active(void)
{
    return bk_lv_tool_ui.music_player != NULL &&
           lv_obj_is_valid(bk_lv_tool_ui.music_player) &&
           lv_screen_active() == bk_lv_tool_ui.music_player;
}

static bool mp_suffix(const char *name, const char *suffix)
{
    size_t a = strlen(name);
    size_t b = strlen(suffix);
    if (a <= b) {
        return false;
    }
    name += a - b;
    while (*suffix) {
        char ca = *name++;
        char cb = *suffix++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (ca != cb) return false;
    }
    return true;
}

static bool mp_audio_file(const char *name)
{
    return mp_suffix(name, ".mp3") || mp_suffix(name, ".wav") || mp_suffix(name, ".aac");
}

static void mp_label(lv_obj_t *label, const char *text)
{
    if (label != NULL && lv_obj_is_valid(label)) {
        lv_label_set_text(label, text != NULL ? text : "");
    }
}

static void mp_apply_font(lv_obj_t *obj, lv_font_t *font)
{
    if (obj != NULL && lv_obj_is_valid(obj) && font != NULL) {
        lv_obj_set_style_text_font(obj, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static bool mp_read_whole_file(const char *path)
{
#if LV_USE_TINY_TTF
    int fd = bk_vfs_open(path, O_RDONLY);
    struct stat st;
    int got;

    if (fd < 0 || bk_vfs_fstat(fd, &st) != 0 || st.st_size <= 0) {
        if (fd >= 0) bk_vfs_close(fd);
        return false;
    }
    s_ttf_data = psram_malloc((size_t)st.st_size);
    if (s_ttf_data == NULL) {
        LOGE("PSRAM allocation failed for %s (%u bytes)\n",
             path, (unsigned)st.st_size);
        bk_vfs_close(fd);
        return false;
    }
    got = bk_vfs_read(fd, s_ttf_data, (size_t)st.st_size);
    bk_vfs_close(fd);
    if (got != st.st_size) {
        psram_free(s_ttf_data);
        s_ttf_data = NULL;
        return false;
    }
    s_ttf_size = (size_t)st.st_size;
    LOGI("loaded CJK font once: %s (%u bytes)\n", path, (unsigned)s_ttf_size);
    return true;
#else
    (void)path;
    return false;
#endif
}

/* Match the reference dashboard: its SD-NAND is exposed to LVGL as S:/. */
static bool mp_read_whole_lv_file(const char *path)
{
#if LV_USE_TINY_TTF
    lv_fs_file_t file;
    uint32_t size = 0;
    uint32_t got = 0;

    if (lv_fs_open(&file, path, LV_FS_MODE_RD) != LV_FS_RES_OK) return false;
    (void)lv_fs_seek(&file, 0, LV_FS_SEEK_END);
    (void)lv_fs_tell(&file, &size);
    (void)lv_fs_seek(&file, 0, LV_FS_SEEK_SET);
    if (size == 0) {
        lv_fs_close(&file);
        return false;
    }
    s_ttf_data = psram_malloc(size);
    if (s_ttf_data == NULL) {
        LOGE("PSRAM allocation failed for %s (%u bytes)\n",
             path, (unsigned)size);
        lv_fs_close(&file);
        return false;
    }
    if (lv_fs_read(&file, s_ttf_data, size, &got) != LV_FS_RES_OK || got != size) {
        lv_fs_close(&file);
        psram_free(s_ttf_data);
        s_ttf_data = NULL;
        return false;
    }
    lv_fs_close(&file);
    s_ttf_size = size;
    LOGI("loaded CJK font once: %s (%u bytes)\n", path, (unsigned)size);
    return true;
#else
    (void)path;
    return false;
#endif
}

static bool mp_load_first_ttf(const char *root)
{
    DIR *dir = bk_vfs_opendir(root);
    struct dirent *entry;
    char path[MP_PATH_MAX];

    if (dir == NULL) return false;
    while ((entry = bk_vfs_readdir(dir)) != NULL) {
        int len;
        if (!mp_suffix(entry->d_name, ".ttf")) continue;
        len = snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        if (len >= 0 && len < (int)sizeof(path) && mp_read_whole_file(path)) {
            bk_vfs_closedir(dir);
            return true;
        }
    }
    bk_vfs_closedir(dir);
    return false;
}

static void mp_fonts_ensure(void)
{
#if LV_USE_TINY_TTF
    if (s_ttf_data == NULL) {
        (void)video_play_engine_api_prepare_storage();
        if (!mp_read_whole_lv_file("S:/SmileySans-Oblique-2.ttf") &&
            !mp_read_whole_lv_file("S:/simhei_new.ttf") &&
            !mp_load_first_ttf("/sd0") && !mp_load_first_ttf("/ud0")) {
            LOGW("CJK font not found on S:/, /sd0 or /ud0\n");
        }
    }
    if (s_ttf_data != NULL && s_ttf_24 == NULL) {
        s_ttf_24 = lv_tiny_ttf_create_data_ex(s_ttf_data, s_ttf_size, 24,
                                               LV_FONT_KERNING_NORMAL,
                                               MP_TTF_CACHE_GLYPHS);
        s_ttf_32 = lv_tiny_ttf_create_data_ex(s_ttf_data, s_ttf_size, 32,
                                               LV_FONT_KERNING_NORMAL,
                                               MP_TTF_CACHE_GLYPHS);
        s_ttf_82 = lv_tiny_ttf_create_data_ex(s_ttf_data, s_ttf_size, 82,
                                               LV_FONT_KERNING_NORMAL,
                                               MP_TTF_CACHE_GLYPHS);
        if (s_ttf_24 == NULL || s_ttf_32 == NULL || s_ttf_82 == NULL) {
            LOGE("tiny_ttf font creation failed (24=%p, 32=%p, 82=%p)\n",
                 s_ttf_24, s_ttf_32, s_ttf_82);
        }
    }
#endif
}

static void mp_apply_runtime_fonts(void)
{
    mp_apply_font(bk_lv_tool_ui.music_player_bt_title_magenta, s_ttf_82);
    mp_apply_font(bk_lv_tool_ui.music_player_bt_title_cyan, s_ttf_82);
    mp_apply_font(bk_lv_tool_ui.music_player_bt_title, s_ttf_82);
    mp_apply_font(bk_lv_tool_ui.music_player_bt_artist, s_ttf_24);
    mp_apply_font(bk_lv_tool_ui.music_player_usb_title, s_ttf_32);
    mp_apply_font(bk_lv_tool_ui.music_player_usb_artist, s_ttf_24);
}

static void mp_ui_now_playing(void)
{
    const mp_track_t *t = NULL;
    if (s_tracks != NULL && s_track_index >= 0 && s_track_index < s_track_count) {
        t = &s_tracks[s_track_index];
    }
    mp_label(bk_lv_tool_ui.music_player_usb_title, t ? t->title : "尚未选择歌曲");
    mp_label(bk_lv_tool_ui.music_player_usb_artist,
             (t && t->artist[0]) ? t->artist : (t ? "未知歌手" : "插入U盘后选择歌曲"));
    mp_label(bk_lv_tool_ui.music_player_usb_play,
             s_local_state == MP_LOCAL_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void mp_request_bt_metadata(void)
{
    (void)bk_avrcp_ct_get_attr(BK_AVRCP_MEDIA_ATTR_ID_TITLE);
    (void)bk_avrcp_ct_get_attr(BK_AVRCP_MEDIA_ATTR_ID_ARTIST);
    (void)bk_avrcp_ct_get_attr(BK_AVRCP_MEDIA_ATTR_ID_PLAYING_TIME);
    LOGI("requested current AVRCP metadata after connection\n");
}

static void mp_sync_bt_connection(void)
{
    uint8_t connected = bk_avrcp_ct_is_connected() ? 1U : 0U;

    if (s_bt.connected == connected) return;
    s_bt.connected = connected;
    s_displayed_bt_title[0] = '\0';
    if (connected) {
        mp_request_bt_metadata();
    } else {
        s_bt.playing = 0;
        s_bt.title[0] = '\0';
        s_bt.artist[0] = '\0';
    }
}

static void mp_ui_bt_state(void)
{
    const char *title =
        s_bt.title[0] ? s_bt.title : (s_bt.connected ? "等待手机发送歌词" : "未连接");

    mp_label(bk_lv_tool_ui.music_player_bt_status,
             s_bt.connected ? "已连接手机 / A2DP" : "等待手机连接 / A2DP SINK");
    if (strcmp(s_displayed_bt_title, title) != 0) {
        static const uint8_t angles[] = {0U, 1U, 2U, 1U};
        snprintf(s_displayed_bt_title, sizeof(s_displayed_bt_title), "%s", title);
        mp_label(bk_lv_tool_ui.music_player_bt_title_magenta, title);
        mp_label(bk_lv_tool_ui.music_player_bt_title_cyan, title);
        mp_label(bk_lv_tool_ui.music_player_bt_title, title);
        s_lyric_fx_phase = 0;
        s_lyric_angle_index =
            angles[s_lyric_angle_sequence++ &
                   ((sizeof(angles) / sizeof(angles[0])) - 1U)];
        /* Start each new line slightly below center. The OSD origin moves up
         * over the next three animation ticks without redrawing the text. */
        klok_mv_render_music_pose(s_lyric_angle_index, 6);
    }
    mp_label(bk_lv_tool_ui.music_player_bt_artist,
             s_bt.artist[0] ? s_bt.artist : "AVRCP");
    mp_label(bk_lv_tool_ui.music_player_bt_play,
             s_bt.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void mp_lyric_fx_tick(void)
{
    static const int8_t intro_offset_y[] = {4, 2, 0};
    uint8_t phase = s_lyric_fx_phase++;

    if (phase < sizeof(intro_offset_y) / sizeof(intro_offset_y[0])) {
        klok_mv_render_music_pose(s_lyric_angle_index, intro_offset_y[phase]);
    }
}

static void mp_lyric_fx_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!mp_active() || !s_bt.playing || s_lyric_fx_phase >= 3U) {
        return;
    }
    mp_lyric_fx_tick();
}

static void mp_bt_ui_async(void *user_data)
{
    (void)user_data;
    if (!mp_active()) return;
    mp_ui_bt_state();
    klok_mv_render_overlay_dirty();
}

static bool mp_background_start(void)
{
    static const char *paths[] = {
        MP_BACKGROUND_FILE,
        "/sd0/Music/musicBackground.mp4",
    };
    const char *path = NULL;
    int prepared;
    int ret;

    if (!s_background_enabled) return false;
    if (video_play_engine_api_prepare_storage() != AVDK_ERR_OK) {
        LOGW("background storage is not ready\n");
        return false;
    }
    for (uint32_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        int fd = bk_vfs_open(paths[i], O_RDONLY);
        if (fd >= 0) {
            bk_vfs_close(fd);
            path = paths[i];
            break;
        }
    }
    if (path == NULL) {
        LOGW("background video not found: %s or %s\n", paths[0], paths[1]);
        s_background_enabled = false;
        return false;
    }

    klok_player_set_repeat(true);
    (void)klok_player_set_muted(true);
    prepared = klok_player_begin_output_switch(KLOK_PLAYER_OUTPUT_FRAME_PREVIEW);
    if (prepared < 0) {
        LOGW("background output switch prepare failed: %s\n", path);
        return false;
    }

    klok_mv_render_prepare_locked();
    if (klok_mv_render_enter_pp_locked() != BK_OK) {
        LOGE("music PP-OSD render enter failed\n");
        if (prepared == 0) {
            klok_player_cancel_output_switch();
        }
        return false;
    }

    if (prepared == 0) {
        ret = klok_player_complete_output_switch(path);
    } else if (prepared == 1) {
        ret = klok_player_play_file(path);
    } else {
        ret = -1;
    }
    if (ret != 0) {
        LOGW("background video start failed: %s, switch=%d\n", path, prepared);
        klok_mv_render_leave_locked();
        return false;
    }
    LOGI("background video playing: %s\n", path);
    return true;
}

static void mp_set_panels(void)
{
    if (bk_lv_tool_ui.music_player_bt_panel == NULL ||
        !lv_obj_is_valid(bk_lv_tool_ui.music_player_bt_panel) ||
        bk_lv_tool_ui.music_player_usb_panel == NULL ||
        !lv_obj_is_valid(bk_lv_tool_ui.music_player_usb_panel)) {
        return;
    }
    if (s_show_udisk) {
        lv_obj_add_flag(bk_lv_tool_ui.music_player_bt_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(bk_lv_tool_ui.music_player_usb_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bk_lv_tool_ui.music_player_usb_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(bk_lv_tool_ui.music_player_bt_panel, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(bk_lv_tool_ui.music_player_bt_tab,
                              lv_color_hex(s_show_udisk ? 0x192344 : 0x693cff),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_lv_tool_ui.music_player_usb_tab,
                              lv_color_hex(s_show_udisk ? 0x693cff : 0x192344),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void mp_clear_tracks(void)
{
    s_track_count = 0;
    s_track_index = -1;
    if (s_player != NULL) {
        bk_audio_player_clear_music_list(s_player);
    }
}

static void mp_player_event(audio_player_event_type_t event, void *extra, void *args)
{
    (void)extra;
    (void)args;
    if (event == AUDIO_PLAYER_EVENT_SONG_START ||
        event == AUDIO_PLAYER_EVENT_SONG_RESUME) {
        s_local_state = MP_LOCAL_PLAYING;
    } else if (event == AUDIO_PLAYER_EVENT_SONG_PAUSE) {
        s_local_state = MP_LOCAL_PAUSED;
    } else if (event == AUDIO_PLAYER_EVENT_SONG_FINISH) {
        if (s_track_count > 0) s_track_index = (s_track_index + 1) % s_track_count;
    } else if (event == AUDIO_PLAYER_EVENT_SONG_FAILURE) {
        s_local_state = MP_LOCAL_STOPPED;
    }
}

static bool mp_player_ensure(void)
{
    bk_audio_player_cfg_t cfg = DEFAULT_AUDIO_PLAYER_CONFIG();
    if (s_player != NULL) return true;
    cfg.event_handler = mp_player_event;
    if (bk_audio_player_new(&s_player, &cfg) != AUDIO_PLAYER_OK) {
        s_player = NULL;
        return false;
    }
    bk_audio_player_register_source(s_player, bk_audio_player_get_file_source_ops());
    bk_audio_player_register_sink(s_player, bk_audio_player_get_onboard_speaker_sink_ops());
    bk_audio_player_register_decoder(s_player, bk_audio_player_get_mp3_decoder_ops());
    bk_audio_player_register_decoder(s_player, bk_audio_player_get_wav_decoder_ops());
    bk_audio_player_register_decoder(s_player, bk_audio_player_get_aac_decoder_ops());
    bk_audio_player_register_metadata_parser(s_player, bk_audio_player_get_mp3_metadata_parser_ops());
    bk_audio_player_register_metadata_parser(s_player, bk_audio_player_get_wav_metadata_parser_ops());
    bk_audio_player_register_metadata_parser(s_player, bk_audio_player_get_aac_metadata_parser_ops());
    bk_audio_player_set_play_mode(s_player, AUDIO_PLAYER_MODE_SEQUENCE_LOOP);
    bk_audio_player_set_volume(s_player, 70);
    {
        bk_audio_player_pa_ctrl_t pa = DEFAULT_AUDIO_PLAYER_PA_CTRL();

        pa.pa_ctrl_en   = true;
        pa.pa_ctrl_gpio = 13;
        pa.pa_on_level  = 1;
        (void)bk_audio_player_set_pa_ctrl(s_player, &pa);
    }
    return true;
}

static void mp_track_from_name(mp_track_t *track, const char *name)
{
    char temp[MP_TITLE_MAX];
    char *dot;
    snprintf(temp, sizeof(temp), "%s", name);
    dot = strrchr(temp, '.');
    if (dot) *dot = '\0';
    snprintf(track->title, sizeof(track->title), "%s", temp);
}

static void mp_scan_tracks(void)
{
    const char *root = "/ud0/Music";
    DIR *dir;
    struct dirent *entry;

    mp_clear_tracks();
    if (s_tracks == NULL || !mp_player_ensure()) return;
    dir = bk_vfs_opendir(root);
    if (dir == NULL) {
        root = "/ud0";
        dir = bk_vfs_opendir(root);
    }
    if (dir == NULL) return;

    while ((entry = bk_vfs_readdir(dir)) != NULL && s_track_count < MP_MAX_TRACKS) {
        mp_track_t *track;
        audio_metadata_t metadata;
        if (!mp_audio_file(entry->d_name)) continue;
        track = &s_tracks[s_track_count];
        memset(track, 0, sizeof(*track));
        snprintf(track->path, sizeof(track->path), "%s/%s", root, entry->d_name);
        mp_track_from_name(track, entry->d_name);
        memset(&metadata, 0, sizeof(metadata));
        if (bk_audio_player_get_metadata_from_file(s_player, track->path, &metadata) == 0) {
            if (metadata.title[0]) snprintf(track->title, sizeof(track->title), "%s", metadata.title);
            if (metadata.artist[0]) snprintf(track->artist, sizeof(track->artist), "%s", metadata.artist);
            if (metadata.duration > 0) track->duration_ms = (uint32_t)metadata.duration;
        }
        bk_audio_player_add_music(s_player, track->title, track->path);
        s_track_count++;
    }
    bk_vfs_closedir(dir);
    LOGI("scanned %d track(s) from %s\n", s_track_count, root);
}

static void mp_row_clicked(lv_event_t *event)
{
    music_player_ui_play_row((uint32_t)(uintptr_t)lv_event_get_user_data(event));
}

static void mp_rebuild_list(void)
{
    lv_obj_t *list = bk_lv_tool_ui.music_player_usb_list;
    char text[MP_TITLE_MAX + MP_ARTIST_MAX + 16];
    if (list == NULL || !lv_obj_is_valid(list)) return;
    lv_obj_clean(list);
    if (!s_udisk_mounted) {
        mp_label(lv_label_create(list), "未检测到U盘");
        return;
    }
    if (s_track_count == 0) {
        mp_label(lv_label_create(list), "Music目录和根目录中没有 mp3/wav/aac");
        return;
    }
    for (int i = 0; i < s_track_count; ++i) {
        lv_obj_t *row = lv_button_create(list);
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_size(row, LV_PCT(100), 58);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1b2850), LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(0x4f4f91), LV_PART_MAIN);
        snprintf(text, sizeof(text), "%02d  %s%s%s", i + 1, s_tracks[i].title,
                 s_tracks[i].artist[0] ? "  ·  " : "",
                 s_tracks[i].artist);
        lv_label_set_text(label, text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(label, LV_PCT(94));
        mp_apply_font(label, s_ttf_24);
        lv_obj_add_event_cb(row, mp_row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

static int mp_mount_udisk(void)
{
    struct bk_fatfs_partition part = {
        .part_type = FATFS_DEVICE,
        .mount_path = VFS_USB_0_PATITION_0,
        .part_dev.device_name = FATFS_DEV_UDISK,
    };
    int ret = bk_fatfs_mount(&part, 1);
    if (ret == 0) {
        s_udisk_mounted = true;
        mp_fonts_ensure();
        mp_apply_runtime_fonts();
        mp_scan_tracks();
        mp_rebuild_list();
    } else {
        LOGE("mount /ud0 failed: %d\n", ret);
    }
    return ret;
}

static void mp_unmount_udisk(void)
{
    if (!s_udisk_mounted) return;
    if (s_player != NULL) bk_audio_player_stop(s_player);
    s_local_state = MP_LOCAL_STOPPED;
    mp_clear_tracks();
    (void)bk_vfs_umount(VFS_USB_0_PATITION_0);
    s_udisk_mounted = false;
    mp_rebuild_list();
    mp_ui_now_playing();
}

static int mp_usb_host_enter(void)
{
#if CONFIG_USB_HOST && CONFIG_USBH_MSC
    int ret;
    if (sdcard_mtp_is_active()) {
        /* MTP device and MSC host share controller 0 and must never overlap. */
        (void)sdcard_mtp_stop();
    }
    if (s_usb_host_open) return BK_OK;
    ret = bk_usb_driver_init();
    if (ret != BK_OK) {
        LOGE("bk_usb_driver_init failed: %d\n", ret);
        return ret;
    }
    ret = bk_usb_open(USB_HOST_MODE);
    if (ret == BK_OK) s_usb_host_open = true;
    else LOGE("bk_usb_open(HOST) failed: %d\n", ret);
    return ret;
#else
    return BK_FAIL;
#endif
}

static void mp_usb_host_leave(void)
{
#if CONFIG_USB_HOST && CONFIG_USBH_MSC
    if (!s_usb_host_open) return;
    mp_unmount_udisk();
    (void)bk_usb_close();
    s_usb_host_open = false;
    LOGI("USB MSC host released; MTP device may start safely\n");
#endif
}

static void mp_poll(lv_timer_t *timer)
{
    static bool last_ready;
    bool ready;
    (void)timer;
    if (!mp_active()) return;
#if CONFIG_USB_HOST && CONFIG_USBH_MSC
    ready = s_usb_host_open && usbh_ms_media_get_status();
#else
    ready = false;
#endif
    if (ready != last_ready) {
        last_ready = ready;
        if (ready) (void)mp_mount_udisk();
        else mp_unmount_udisk();
    }
    if (s_ttf_24 == NULL && ++s_font_retry_ticks >= 10) {
        s_font_retry_ticks = 0;
        mp_fonts_ensure();
        mp_apply_runtime_fonts();
    }
    if (s_background_enabled && !klok_player_is_started() &&
        ++s_bg_retry_ticks >= MP_BG_RETRY_TICKS) {
        s_bg_retry_ticks = 0;
        (void)mp_background_start();
    }
    mp_sync_bt_connection();
    mp_ui_bt_state();
    mp_ui_now_playing();
}

static size_t mp_utf8_put(char *dst, size_t cap, size_t pos, uint32_t cp)
{
    if (cp < 0x80 && pos + 1 < cap) dst[pos++] = (char)cp;
    else if (cp < 0x800 && pos + 2 < cap) {
        dst[pos++] = (char)(0xc0 | (cp >> 6));
        dst[pos++] = (char)(0x80 | (cp & 0x3f));
    } else if (pos + 3 < cap) {
        dst[pos++] = (char)(0xe0 | (cp >> 12));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        dst[pos++] = (char)(0x80 | (cp & 0x3f));
    }
    dst[pos] = '\0';
    return pos;
}

static void mp_copy_avrcp(char *dst, size_t cap, const a2dp_sink_avrcp_attr_t *attr)
{
    size_t pos = 0;
    if (cap == 0 || attr == NULL || attr->attr_text == NULL) return;
    dst[0] = '\0';
    if (attr->attr_text_charset == 0x03e8 || attr->attr_text_charset == 0x03f5 ||
        attr->attr_text_charset == 0x03f6 || attr->attr_text_charset == 0x03f7) {
        bool le = attr->attr_text_charset == 0x03f6;
        for (uint32_t i = 0; i + 1 < attr->attr_length; i += 2) {
            uint32_t cp = le ? attr->attr_text[i] | ((uint32_t)attr->attr_text[i + 1] << 8)
                             : ((uint32_t)attr->attr_text[i] << 8) | attr->attr_text[i + 1];
            if (cp == 0) break;
            pos = mp_utf8_put(dst, cap, pos, cp);
        }
    } else {
        size_t n = attr->attr_length < cap - 1 ? attr->attr_length : cap - 1;
        memcpy(dst, attr->attr_text, n);
        dst[n] = '\0';
    }
}

static void mp_bt_event(a2dp_sink_ui_event_t event, const void *data, void *user)
{
    bool changed = false;
    (void)user;
    if (event == A2DP_SINK_UI_EVT_ELEM_ATTR_RSP && data != NULL) {
        const a2dp_sink_avrcp_elem_attr_msg_t *rsp = data;
        s_bt.connected = 1;
        for (uint32_t i = 0; i < rsp->attr_count; ++i) {
            if (rsp->attr_array[i].attr_id == BK_AVRCP_MEDIA_ATTR_ID_TITLE) {
                char title[MP_TITLE_MAX] = {0};
                mp_copy_avrcp(title, sizeof(title), &rsp->attr_array[i]);
                if (strcmp((const char *)s_bt.title, title) != 0) {
                    snprintf((char *)s_bt.title, sizeof(s_bt.title), "%s", title);
                    changed = true;
                }
            } else if (rsp->attr_array[i].attr_id == BK_AVRCP_MEDIA_ATTR_ID_ARTIST) {
                mp_copy_avrcp((char *)s_bt.artist, sizeof(s_bt.artist), &rsp->attr_array[i]);
            }
        }
    } else if (event == A2DP_SINK_UI_EVT_PLAY_STATUS_CHANGED && data != NULL) {
        s_bt.connected = 1;
        s_bt.playing = (*(const uint8_t *)data == BK_AVRCP_PLAYBACK_PLAYING);
        if (s_bt.playing && s_player != NULL) {
            bk_audio_player_stop(s_player);
            s_local_state = MP_LOCAL_STOPPED;
        }
    } else if (event == A2DP_SINK_UI_EVT_TRACK_CHANGED) {
        s_bt.connected = 1;
    } else if (event == A2DP_SINK_UI_EVT_DISCONNECTED) {
        memset((void *)&s_bt, 0, sizeof(s_bt));
        changed = true;
    }
    if (changed) {
        (void)lv_async_call(mp_bt_ui_async, NULL);
    }
}

void music_player_ui_register_bt_callback(void)
{
    const a2dp_sink_ui_callback_t cb = {.event = mp_bt_event, .user_data = NULL};
    a2dp_sink_demo_register_ui_callback(&cb);
}

void music_player_ui_select_bluetooth(void)
{
    s_show_udisk = false;
    if (s_player != NULL) bk_audio_player_stop(s_player);
    s_local_state = MP_LOCAL_STOPPED;
    mp_usb_host_leave();
    a2dp_sink_demo_audio_spk_enable(1);
    mp_set_panels();
}

void music_player_ui_select_udisk(void)
{
    s_show_udisk = true;
    /* Keep A2DP connected for AVRCP state, but release its speaker pipeline. */
    a2dp_sink_demo_audio_spk_enable(0);
    (void)mp_usb_host_enter();
    mp_set_panels();
}

void music_player_ui_play_row(uint32_t index)
{
    if (index >= (uint32_t)s_track_count || !mp_player_ensure()) return;
    a2dp_sink_demo_audio_spk_enable(0);
    if (bk_audio_player_jumpto(s_player, (int)index) == AUDIO_PLAYER_OK) {
        s_track_index = (int)index;
        s_local_state = MP_LOCAL_PLAYING;
        mp_ui_now_playing();
    }
}

void music_player_ui_local_prev(void)
{
    if (s_player != NULL && s_track_count > 0) {
        a2dp_sink_demo_audio_spk_enable(0);
        bk_audio_player_prev(s_player);
        s_track_index = (s_track_index + s_track_count - 1) % s_track_count;
    }
}

void music_player_ui_local_toggle(void)
{
    if (s_local_state == MP_LOCAL_PLAYING) {
        bk_audio_player_pause(s_player);
    } else if (s_local_state == MP_LOCAL_PAUSED) {
        bk_audio_player_resume(s_player);
    } else if (s_track_count > 0) {
        music_player_ui_play_row(s_track_index >= 0 ? (uint32_t)s_track_index : 0);
    }
}

void music_player_ui_local_next(void)
{
    if (s_player != NULL && s_track_count > 0) {
        a2dp_sink_demo_audio_spk_enable(0);
        bk_audio_player_next(s_player);
        s_track_index = (s_track_index + 1) % s_track_count;
    }
}

void music_player_ui_bt_prev(void) { (void)bk_avrcp_ct_prev(); }
void music_player_ui_bt_next(void) { (void)bk_avrcp_ct_next(); }
void music_player_ui_bt_toggle(void)
{
    if (!bk_avrcp_ct_is_connected()) return;
    if (s_player != NULL) bk_audio_player_stop(s_player);
    s_local_state = MP_LOCAL_STOPPED;
    a2dp_sink_demo_audio_spk_enable(1);
    if (s_bt.playing) (void)bk_avrcp_ct_pause();
    else (void)bk_avrcp_ct_play();
}

bool music_player_ui_is_active(void)
{
    return mp_active();
}

void music_player_ui_key_prev(void)
{
    if (s_show_udisk) music_player_ui_local_prev();
    else music_player_ui_bt_prev();
}

void music_player_ui_key_toggle(void)
{
    if (s_show_udisk) music_player_ui_local_toggle();
    else music_player_ui_bt_toggle();
}

void music_player_ui_key_next(void)
{
    if (s_show_udisk) music_player_ui_local_next();
    else music_player_ui_bt_next();
}

void music_player_ui_enter(void)
{
    klok_mv_render_leave_locked();
    klok_mv_render_use_music_overlay(true);
    klok_lvgl_preview_release_locked();
    a2dp_sink_demo_audio_spk_enable(1);
    if (s_tracks == NULL) {
        s_tracks = psram_malloc(sizeof(mp_track_t) * MP_MAX_TRACKS);
    }
    s_show_udisk = false;
    s_font_retry_ticks = 0;
    s_bg_retry_ticks = 0;
    s_lyric_fx_phase = 0;
    s_lyric_angle_index = 1U;
    s_lyric_angle_sequence = 0U;
    s_displayed_bt_title[0] = '\0';
    s_background_enabled = true;
    mp_sync_bt_connection();
    mp_fonts_ensure();
    mp_apply_runtime_fonts();
    mp_set_panels();
    mp_ui_bt_state();
    mp_ui_now_playing();
    if (s_poll_timer == NULL) s_poll_timer = lv_timer_create(mp_poll, MP_POLL_MS, NULL);
    if (s_lyric_fx_timer == NULL) {
        s_lyric_fx_timer = lv_timer_create(mp_lyric_fx_timer_cb,
                                           MP_LYRIC_FX_MS,
                                           NULL);
    }
    (void)mp_background_start();
}

void music_player_ui_leave(void)
{
    s_background_enabled = false;
    klok_player_set_repeat(false);
    if (s_poll_timer != NULL) {
        lv_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }
    if (s_lyric_fx_timer != NULL) {
        lv_timer_delete(s_lyric_fx_timer);
        s_lyric_fx_timer = NULL;
    }
    if (s_player != NULL) bk_audio_player_stop(s_player);
    s_local_state = MP_LOCAL_STOPPED;
    mp_usb_host_leave();
    a2dp_sink_demo_audio_spk_enable(1);
    (void)klok_player_stop();
    (void)klok_player_set_muted(false);
    klok_mv_render_leave_locked();
    klok_mv_render_use_music_overlay(false);
    klok_lvgl_preview_release_locked();
}

static void mp_leave_to_home_finish(void *user_data)
{
    (void)user_data;
    music_player_ui_leave();
    navigate_to_screen(&bk_lv_tool_ui.home,
                       LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                       220,
                       0,
                       false,
                       init_page_home);
    s_leave_pending = false;
    LOGI("async return to home complete\n");
}

static void mp_leave_to_home_worker(void *user_data)
{
    (void)user_data;

    /*
     * Stop outside LVGL's display lock. The video display worker may need that
     * lock to finish an already queued preview callback before it can exit.
     */
    (void)klok_player_stop();

    lv_vendor_disp_lock();
    lv_result_t ret = lv_async_call(mp_leave_to_home_finish, NULL);
    lv_vendor_disp_unlock();
    if (ret != LV_RESULT_OK) {
        s_leave_pending = false;
        LOGE("failed to schedule return to home\n");
    }
    rtos_delete_thread(NULL);
}

void music_player_ui_leave_to_home_async(void)
{
    if (s_leave_pending) {
        return;
    }
    s_leave_pending = true;

    beken_thread_t worker = NULL;
    if (rtos_create_thread(&worker,
                           BEKEN_DEFAULT_WORKER_PRIORITY,
                           "music_leave",
                           (beken_thread_function_t)mp_leave_to_home_worker,
                           4096,
                           NULL) != BK_OK) {
        s_leave_pending = false;
        LOGE("failed to create music leave worker\n");
    }
}
