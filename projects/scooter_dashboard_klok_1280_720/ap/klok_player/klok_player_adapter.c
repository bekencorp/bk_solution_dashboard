#include "klok_player_adapter.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <components/system.h>
#include "bk_posix.h"
#include "video_play_engine_api.h"

#ifndef KLOK_PLAYER_DEFAULT_FILE
#define KLOK_PLAYER_DEFAULT_FILE "/sd0/klok_demo.mp4"
#endif

#define KLOK_PLAYER_DEFAULT_VOLUME 8U
#define KLOK_PLAYER_VOLUME_STEP 5U
#define KLOK_PLAYER_PATH_MAX 256
#define KLOK_PLAYER_MEDIA_DIR "/sd0"
#define KLOK_PLAYER_MAX_MEDIA_FILES 64
#define KLOK_PLAYER_MEDIA_NAME_MAX 128
#define KLOK_PLAYER_ACCOMPANY_TRACK 1U
#define KLOK_PLAYER_VOCAL_TRACK 0U

static bool s_started = false;
static bool s_paused = false;
static bool s_muted = false;
static uint8_t s_volume = KLOK_PLAYER_DEFAULT_VOLUME;
static uint8_t s_audio_track = KLOK_PLAYER_VOCAL_TRACK;
static char s_current_file_path[KLOK_PLAYER_PATH_MAX] = KLOK_PLAYER_DEFAULT_FILE;
static const char *s_current_file = s_current_file_path;
static bool s_media_cache_valid = false;
static uint16_t s_media_count = 0;
static int s_current_media_index = -1;
static char s_media_names[KLOK_PLAYER_MAX_MEDIA_FILES][KLOK_PLAYER_MEDIA_NAME_MAX];
static bool s_output_switching = false;
static klok_player_finished_cb_t s_finished_callback = NULL;
static void *s_finished_user_data = NULL;

static void klok_player_on_finished(const char *file_path, void *user_data)
{
    (void)user_data;

    if (file_path != NULL &&
        strcmp(file_path, s_current_file_path) != 0) {
        bk_printf("klok player: ignore stale finish event: %s\r\n", file_path);
        return;
    }

    s_started = false;
    s_paused = false;
    s_output_switching = false;
    bk_printf("klok player: playback finished: %s\r\n",
              file_path != NULL ? file_path : s_current_file_path);

    if (s_finished_callback != NULL) {
        s_finished_callback(file_path, s_finished_user_data);
    }
}

static int klok_player_result(avdk_err_t ret)
{
    return ret == AVDK_ERR_OK ? 0 : -1;
}

static bool klok_player_has_media_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }

    dot++;
    char ext[8] = {0};
    size_t i = 0;
    while (dot[i] != '\0' && i < sizeof(ext) - 1U) {
        char c = dot[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        ext[i] = c;
        i++;
    }

    return strcmp(ext, "mp4") == 0 ||
           strcmp(ext, "avi") == 0 ||
           strcmp(ext, "mkv") == 0 ||
           strcmp(ext, "mov") == 0;
}

static const char *klok_player_current_file_name(void)
{
    const char *slash = strrchr(s_current_file_path, '/');
    return (slash != NULL) ? (slash + 1) : s_current_file_path;
}

static int klok_player_find_cached_media_index(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    for (uint16_t i = 0; i < s_media_count; i++) {
        if (strcmp(s_media_names[i], name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static void klok_player_update_current_media_index(void)
{
    if (!s_media_cache_valid) {
        s_current_media_index = -1;
        return;
    }

    s_current_media_index = klok_player_find_cached_media_index(klok_player_current_file_name());
}

static int klok_player_build_media_path(const char *name, char *path, size_t path_size)
{
    int len;

    if (name == NULL || path == NULL || path_size == 0U) {
        return -1;
    }

    len = snprintf(path, path_size, "%s/%s", KLOK_PLAYER_MEDIA_DIR, name);
    return (len < 0 || (size_t)len >= path_size) ? -1 : 0;
}

static int klok_player_refresh_media_cache(void)
{
    if (video_play_engine_api_prepare_storage() != AVDK_ERR_OK) {
        bk_printf("klok media cache: failed to mount %s\r\n", KLOK_PLAYER_MEDIA_DIR);
        return -1;
    }

    DIR *dir = opendir(KLOK_PLAYER_MEDIA_DIR);
    struct dirent *entry = NULL;
    uint16_t count = 0;

    if (dir == NULL) {
        bk_printf("klok media cache: failed to open %s\r\n", KLOK_PLAYER_MEDIA_DIR);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !klok_player_has_media_ext(entry->d_name)) {
            continue;
        }

        if (count >= KLOK_PLAYER_MAX_MEDIA_FILES) {
            bk_printf("klok media cache: too many media files, keep first %u\r\n",
                      (unsigned)KLOK_PLAYER_MAX_MEDIA_FILES);
            break;
        }

        if (strlen(entry->d_name) >= KLOK_PLAYER_MEDIA_NAME_MAX) {
            bk_printf("klok media cache: skip long file name: %s\r\n", entry->d_name);
            continue;
        }

        snprintf(s_media_names[count], sizeof(s_media_names[count]), "%s", entry->d_name);
        count++;
    }

    closedir(dir);

    s_media_count = count;
    s_media_cache_valid = true;
    klok_player_update_current_media_index();

    if (s_media_count == 0) {
        bk_printf("klok media cache: no media file found in %s\r\n", KLOK_PLAYER_MEDIA_DIR);
        return -1;
    }

    return 0;
}

static int klok_player_ensure_media_cache(void)
{
    if (!s_media_cache_valid || s_media_count == 0) {
        return klok_player_refresh_media_cache();
    }

    return 0;
}

static int klok_player_find_relative_file(int direction, char *path, size_t path_size)
{
    if (klok_player_ensure_media_cache() != 0) {
        return -1;
    }

    int current_index = s_current_media_index;
    if (current_index < 0 ||
        current_index >= (int)s_media_count ||
        strcmp(s_media_names[current_index], klok_player_current_file_name()) != 0) {
        current_index = klok_player_find_cached_media_index(klok_player_current_file_name());
    }

    if (current_index < 0) {
        if (klok_player_refresh_media_cache() != 0) {
            return -1;
        }
        current_index = klok_player_find_cached_media_index(klok_player_current_file_name());
    }

    uint16_t target_index;
    if (current_index < 0) {
        target_index = (direction < 0) ? (uint16_t)(s_media_count - 1U) : 0U;
    } else if (direction < 0) {
        target_index = (uint16_t)((current_index + (int)s_media_count - 1) % (int)s_media_count);
    } else {
        target_index = (uint16_t)((current_index + 1) % (int)s_media_count);
    }

    return klok_player_build_media_path(s_media_names[target_index], path, path_size);
}

static int klok_player_start_file(const char *file_path)
{
    if (file_path == NULL || file_path[0] == '\0') {
        return -1;
    }

    if (file_path != s_current_file_path) {
        snprintf(s_current_file_path, sizeof(s_current_file_path), "%s", file_path);
    }

    int ret = klok_player_result(
        video_play_engine_api_start(s_current_file_path));

    if (ret == 0) {
        s_current_file = s_current_file_path;
        s_started = true;
        s_paused = false;
        klok_player_update_current_media_index();
        if (s_audio_track != KLOK_PLAYER_VOCAL_TRACK) {
            (void)video_play_engine_api_select_audio_track(s_audio_track);
        }
        (void)video_play_engine_api_set_volume(s_volume);
        if (s_muted) {
            (void)video_play_engine_api_set_mute(true);
        }
    }

    return ret;
}

void klok_player_adapter_init(void)
{
    s_started = false;
    s_paused = false;
    s_muted = false;
    s_volume = KLOK_PLAYER_DEFAULT_VOLUME;
    s_audio_track = KLOK_PLAYER_VOCAL_TRACK;
    snprintf(s_current_file_path, sizeof(s_current_file_path), "%s", KLOK_PLAYER_DEFAULT_FILE);
    s_current_file = s_current_file_path;
    s_media_cache_valid = false;
    s_media_count = 0;
    s_current_media_index = -1;
    s_output_switching = false;
    s_finished_callback = NULL;
    s_finished_user_data = NULL;
    video_play_engine_api_set_finished_callback(klok_player_on_finished, NULL);
}

void klok_player_set_finished_callback(klok_player_finished_cb_t callback,
                                       void *user_data)
{
    s_finished_callback = callback;
    s_finished_user_data = user_data;
}

int klok_player_play_default(void)
{
    if (s_started && s_paused) {
        int ret = klok_player_result(
            video_play_engine_api_set_pause(false));
        if (ret == 0) {
            s_paused = false;
        }
        return ret;
    }

    return klok_player_start_file(KLOK_PLAYER_DEFAULT_FILE);
}

int klok_player_play_file(const char *file_path)
{
    return klok_player_start_file(file_path);
}

int klok_player_replay(void)
{
    /*
     * Controller seek(0) can restart audio while leaving the hardware H.264
     * decoder without new output frames. A full play_file restart resets the
     * parser, both decoders and their queues, so audio and video restart
     * together.
     */
    return klok_player_start_file(s_current_file);
}

int klok_player_pause(void)
{
    if (!s_started) {
        return -1;
    }
    if (s_paused) {
        return 0;
    }

    int ret = klok_player_result(video_play_engine_api_set_pause(true));
    if (ret == 0) {
        s_paused = true;
    }
    return ret;
}

int klok_player_resume(void)
{
    if (!s_started) {
        return klok_player_start_file(s_current_file);
    }
    if (!s_paused) {
        return 0;
    }

    int ret = klok_player_result(video_play_engine_api_set_pause(false));
    if (ret == 0) {
        s_paused = false;
    }
    return ret;
}

int klok_player_pause_toggle(void)
{
    if (!s_started) {
        return klok_player_start_file(s_current_file);
    }

    if (s_paused) {
        return klok_player_resume();
    } else {
        return klok_player_pause();
    }
}

bool klok_player_is_started(void)
{
    return s_started;
}

bool klok_player_is_paused(void)
{
    return s_paused;
}

int klok_player_stop(void)
{
    int ret = klok_player_result(video_play_engine_api_stop());

    if (ret == 0) {
        s_started = false;
        s_paused = false;
        s_output_switching = false;
    }

    return ret;
}

int klok_player_next(void)
{
    char next_file[KLOK_PLAYER_PATH_MAX] = {0};

    if (klok_player_find_relative_file(1, next_file, sizeof(next_file)) != 0) {
        return -1;
    }

    return klok_player_start_file(next_file);
}

int klok_player_previous(void)
{
    char previous_file[KLOK_PLAYER_PATH_MAX] = {0};

    if (klok_player_find_relative_file(-1, previous_file, sizeof(previous_file)) != 0) {
        return -1;
    }

    return klok_player_start_file(previous_file);
}

int klok_player_set_volume(uint8_t volume)
{
    if (volume > 100U) {
        volume = 100U;
    }

    s_volume = volume;
    if (!s_started) {
        s_muted = false;
        return 0;
    }

    int ret = klok_player_result(video_play_engine_api_set_volume(volume));
    if (ret == 0 && s_muted) {
        ret = klok_player_result(video_play_engine_api_set_mute(false));
        if (ret == 0) {
            s_muted = false;
        }
    }
    return ret;
}

uint8_t klok_player_get_volume(void)
{
    return s_volume;
}

int klok_player_volume_up(void)
{
    uint16_t volume = (uint16_t)s_volume + KLOK_PLAYER_VOLUME_STEP;
    if (volume > 100U) {
        volume = 100U;
    }
    return klok_player_set_volume((uint8_t)volume);
}

int klok_player_volume_down(void)
{
    uint8_t volume = s_volume > KLOK_PLAYER_VOLUME_STEP
                         ? (uint8_t)(s_volume - KLOK_PLAYER_VOLUME_STEP)
                         : 0U;
    return klok_player_set_volume(volume);
}

int klok_player_mute_toggle(void)
{
    s_muted = !s_muted;
    if (!s_started) {
        return 0;
    }

    int ret = klok_player_result(
        video_play_engine_api_set_mute(s_muted));

    if (ret != 0) {
        s_muted = !s_muted;
    }

    return ret;
}

bool klok_player_is_muted(void)
{
    return s_muted;
}

int klok_player_audio_track(uint8_t index)
{
    int ret = klok_player_result(
        video_play_engine_api_select_audio_track(index));
    if (ret == 0) {
        s_audio_track = index;
    }
    return ret;
}

int klok_player_accompany(void)
{
    return klok_player_audio_track(KLOK_PLAYER_ACCOMPANY_TRACK);
}

int klok_player_vocal(void)
{
    return klok_player_audio_track(KLOK_PLAYER_VOCAL_TRACK);
}

bool klok_player_is_accompany(void)
{
    return s_audio_track == KLOK_PLAYER_ACCOMPANY_TRACK;
}

klok_player_output_mode_t klok_player_get_output_mode(void)
{
    return video_play_engine_api_get_output_mode() ==
                   VIDEO_PLAY_OUTPUT_FRAME_PREVIEW
               ? KLOK_PLAYER_OUTPUT_FRAME_PREVIEW
               : KLOK_PLAYER_OUTPUT_FLEXA_DIRECT;
}

bool klok_player_is_switching(void)
{
    return s_output_switching || video_play_engine_api_is_switching();
}

int klok_player_begin_output_switch(klok_player_output_mode_t target_mode)
{
    if (klok_player_is_switching()) {
        return -1;
    }

    video_play_output_mode_t engine_mode =
        target_mode == KLOK_PLAYER_OUTPUT_FRAME_PREVIEW
            ? VIDEO_PLAY_OUTPUT_FRAME_PREVIEW
            : VIDEO_PLAY_OUTPUT_FLEXA_DIRECT;
    if (video_play_engine_api_get_output_mode() == engine_mode) {
        return 1;
    }

    avdk_err_t ret = video_play_engine_api_begin_output_switch(engine_mode);
    if (ret != AVDK_ERR_OK) {
        return -1;
    }
    s_output_switching = true;
    return 0;
}

int klok_player_complete_output_switch(const char *new_file_path)
{
    if (!s_output_switching || !video_play_engine_api_is_switching()) {
        return -1;
    }

    bool changing_file =
        new_file_path != NULL && new_file_path[0] != '\0';
    avdk_err_t ret = video_play_engine_api_complete_output_switch(
        changing_file ? new_file_path : NULL,
        s_paused);
    s_output_switching = false;
    if (ret != AVDK_ERR_OK) {
        s_started = false;
        return -1;
    }

    if (changing_file) {
        snprintf(s_current_file_path,
                 sizeof(s_current_file_path),
                 "%s",
                 new_file_path);
        s_current_file = s_current_file_path;
        s_started = true;
        s_paused = false;
        klok_player_update_current_media_index();
    }

    if (s_started) {
        /*
         * A same-file output switch keeps the active audio decoder/track and
         * audio device alive. Re-select audio only for a new file; doing it for
         * a video-only switch would reset the audio half-pipeline.
         */
        if (changing_file && s_audio_track != KLOK_PLAYER_VOCAL_TRACK) {
            (void)video_play_engine_api_select_audio_track(s_audio_track);
        }
        (void)video_play_engine_api_set_volume(s_volume);
        if (s_muted) {
            (void)video_play_engine_api_set_mute(true);
        }
    }
    return 0;
}

void klok_player_cancel_output_switch(void)
{
    video_play_engine_api_cancel_output_switch();
    s_output_switching = false;
}
