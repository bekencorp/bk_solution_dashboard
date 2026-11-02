#include "music_player_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <os/os.h>
#include <os/mem.h>

#include "beken_ui.h"
#include "home_ui.h"
#include "lvgl.h"
#include "components/log.h"
#include "bk_vfs.h"
#include "bk_std_header.h"
#include "boot_sd_mount.h"

/* SDK audio player: metadata parsers (used at scan time to read title/artist/
 * duration) plus the full playback API and the plugins we register for local
 * SD-card playback (file source, onboard-speaker sink, mp3/wav/aac decoders). */
#include "components/bk_audio_player/bk_audio_player_types.h"
#include "components/bk_audio_player/bk_audio_player.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_mp3_metadata_parser.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_wav_metadata_parser.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_flac_metadata_parser.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_m4a_metadata_parser.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_aac_metadata_parser.h"
#include "components/bk_audio_player/plugins/metadata_parsers/bk_audio_player_ogg_metadata_parser.h"
#include "components/bk_audio_player/plugins/sources/bk_audio_player_file_source.h"
#include "components/bk_audio_player/plugins/sinks/bk_audio_player_onboard_speaker_sink.h"
#include "components/bk_audio_player/plugins/decoders/bk_audio_player_mp3_decoder.h"
#include "components/bk_audio_player/plugins/decoders/bk_audio_player_wav_decoder.h"
#include "components/bk_audio_player/plugins/decoders/bk_audio_player_aac_decoder.h"

#define TAG "music_player_ui"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* SD music library. */
#define MP_MUSIC_DIR    "/sd0/Music"
#define MP_MAX_TRACKS   100          /* cap dynamic rows (AP SRAM heap bound) */
#define MP_TITLE_MAX    64
#define MP_ARTIST_MAX   40
#define MP_PATH_MAX     160
#define MP_VOLUME_STEP  10

/* Playlist row geometry (mirrors the designer's static rows). */
#define MP_ROW_W        420
#define MP_ROW_H        96

typedef struct {
    char     title[MP_TITLE_MAX];
    char     artist[MP_ARTIST_MAX];
    char     path[MP_PATH_MAX];
    uint32_t dur_ms;                 /* track duration in ms (0 = unknown) */
} mp_track_t;

/* Which area the physical key currently drives. */
typedef enum {
    MP_FOCUS_NONE = 0,   /* whole page: single press returns home */
    MP_FOCUS_NP,         /* now-playing panel focused */
    MP_FOCUS_PL,         /* playlist focused: single press scrolls it */
} mp_focus_t;

/* Runtime CJK fonts built from the shared PSRAM TTF (see home_ui_create_cn_font).
 * Created once and kept for the process lifetime (never freed). */
static lv_font_t *s_cn_30;   /* np song title + playlist row title */
static lv_font_t *s_cn_24;   /* np song artist + playlist row index/artist/duration/count/empty */

static mp_track_t s_tracks[MP_MAX_TRACKS];
static int        s_track_cnt;

static mp_focus_t s_focus;
static int        s_pl_sel;
static int        s_pl_rows;   /* real (selectable) playlist rows */
static int        s_np_sel;    /* selected np-panel button when focus==MP_FOCUS_NP */
static audio_player_mode_t s_play_mode = AUDIO_PLAYER_MODE_SEQUENCE_LOOP;
static bool       s_volume_btns_bound;

/* np-panel control buttons cycled by single-press while the panel is focused. */
#define MP_NP_BTN_CNT 6

/* Playback lifecycle (single 3-state machine, replaces separate playing/paused
 * booleans that could only ever hold 3 of their 4 combinations). */
typedef enum {
    MP_PB_STOPPED = 0,   /* nothing loaded / stopped */
    MP_PB_PLAYING,       /* actively playing */
    MP_PB_PAUSED,        /* paused (resumable) */
} mp_pb_t;

/* Playback (bk_audio_player). The instance is created lazily on first play and
 * kept for the process lifetime; leaving the page only stops it. */
static bk_audio_player_handle_t s_player;
static int          s_now_idx = -1;   /* track index currently loaded in player */
static mp_pb_t      s_pb;             /* playback state, see mp_pb_t */
static volatile int s_tick_sec;       /* last SONG_TICK second (event->LVGL) */
/* ------------------------------------------------------------------ */
/* Fonts                                                              */
/* ------------------------------------------------------------------ */

static void mp_set_font(lv_obj_t *obj, lv_font_t *font)
{
    if (obj != NULL && lv_obj_is_valid(obj) && font != NULL)
    {
        lv_obj_set_style_text_font(obj, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void mp_fonts_ensure(void)
{
    if (s_cn_30 == NULL)
    {
        s_cn_30 = home_ui_create_cn_font(30);
    }
    if (s_cn_24 == NULL)
    {
        s_cn_24 = home_ui_create_cn_font(24);
    }
}

/* ------------------------------------------------------------------ */
/* SD scan                                                            */
/* ------------------------------------------------------------------ */

static char mp_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive check that @name ends with @ext (which must be lowercase). */
static bool mp_ends_with(const char *name, const char *ext)
{
    size_t nlen = strlen(name);
    size_t elen = strlen(ext);
    const char *p;

    if (nlen <= elen)
    {
        return false;
    }
    p = name + nlen - elen;
    for (size_t i = 0; i < elen; i++)
    {
        if (mp_lower(p[i]) != ext[i])
        {
            return false;
        }
    }
    return true;
}

static bool mp_has_audio_suffix(const char *name)
{
    static const char *const exts[] = {
        ".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg", ".ape",
    };

    if (name == NULL)
    {
        return false;
    }
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
    {
        if (mp_ends_with(name, exts[i]))
        {
            return true;
        }
    }
    return false;
}

/* Split "Artist - Title.ext" into artist/title; otherwise title = base name
 * (extension stripped) and artist stays empty. */
static void mp_parse_name(const char *fname, mp_track_t *t)
{
    char base[MP_TITLE_MAX + MP_ARTIST_MAX];
    char *dot;
    char *sep;

    snprintf(base, sizeof(base), "%s", fname);

    dot = strrchr(base, '.');
    if (dot != NULL)
    {
        *dot = '\0';   /* strip extension */
    }

    sep = strstr(base, " - ");
    if (sep != NULL)
    {
        *sep = '\0';
        snprintf(t->artist, sizeof(t->artist), "%s", base);
        snprintf(t->title, sizeof(t->title), "%s", sep + 3);
    }
    else
    {
        t->artist[0] = '\0';
        snprintf(t->title, sizeof(t->title), "%s", base);
    }
}

/* Reject non-UTF-8 byte sequences: the SDK ID3 parser copies Latin-1 / UTF-16
 * tags without transcoding, which the LVGL TTF cannot render. Only accept clean
 * UTF-8 (which covers ASCII and properly-tagged Chinese). */
static bool mp_is_valid_utf8(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;

    while (*p != 0)
    {
        if (*p < 0x80)
        {
            p += 1;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            if ((p[1] & 0xC0) != 0x80) { return false; }
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) { return false; }
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 ||
                (p[3] & 0xC0) != 0x80) { return false; }
            p += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

/* Pick the tag parser for a file by extension. */
static const bk_audio_player_metadata_parser_ops_t *mp_parser_for(const char *name)
{
    if (mp_ends_with(name, ".mp3"))  { return bk_audio_player_get_mp3_metadata_parser_ops(); }
    if (mp_ends_with(name, ".wav"))  { return bk_audio_player_get_wav_metadata_parser_ops(); }
    if (mp_ends_with(name, ".flac")) { return bk_audio_player_get_flac_metadata_parser_ops(); }
    if (mp_ends_with(name, ".m4a"))  { return bk_audio_player_get_m4a_metadata_parser_ops(); }
    if (mp_ends_with(name, ".aac"))  { return bk_audio_player_get_aac_metadata_parser_ops(); }
    if (mp_ends_with(name, ".ogg"))  { return bk_audio_player_get_ogg_metadata_parser_ops(); }
    return NULL;
}

/* Append one Unicode code point to @dst as UTF-8; returns the new length. */
static int mp_utf8_put(char *dst, int cap, int pos, uint32_t cp)
{
    if (cp < 0x80)
    {
        if (pos + 1 > cap) { return pos; }
        dst[pos++] = (char)cp;
    }
    else if (cp < 0x800)
    {
        if (pos + 2 > cap) { return pos; }
        dst[pos++] = (char)(0xC0 | (cp >> 6));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        if (pos + 3 > cap) { return pos; }
        dst[pos++] = (char)(0xE0 | (cp >> 12));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        if (pos + 4 > cap) { return pos; }
        dst[pos++] = (char)(0xF0 | (cp >> 18));
        dst[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    }
    return pos;
}

/* Decode an ID3v2 text-frame body (data[0] = encoding) into UTF-8. Handles the
 * four ID3v2 encodings correctly, unlike the SDK parser which mangles UTF-16
 * (the encoding these Chinese MP3s actually use). */
static void mp_id3_text_to_utf8(const uint8_t *data, uint32_t size, char *dst, int cap)
{
    uint8_t enc;
    const uint8_t *p;
    uint32_t n;
    uint32_t i;
    int pos = 0;

    if (cap <= 0) { return; }
    dst[0] = '\0';
    if (size < 1) { return; }

    enc = data[0];
    p   = data + 1;
    n   = size - 1;

    if (enc == 0)            /* ISO-8859-1 (Latin-1) */
    {
        for (i = 0; i < n && p[i] != 0; i++)
        {
            pos = mp_utf8_put(dst, cap - 1, pos, p[i]);
        }
    }
    else if (enc == 3)       /* UTF-8 */
    {
        for (i = 0; i < n && p[i] != 0 && pos < cap - 1; i++)
        {
            dst[pos++] = (char)p[i];
        }
    }
    else if (enc == 1 || enc == 2)   /* UTF-16 (BOM) / UTF-16BE */
    {
        bool be = (enc == 2);

        i = 0;
        if (enc == 1 && n >= 2)
        {
            if (p[0] == 0xFF && p[1] == 0xFE) { be = false; i = 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { be = true; i = 2; }
        }
        for (; i + 1 < n; i += 2)
        {
            uint32_t u = be ? ((uint32_t)p[i] << 8 | p[i + 1])
                            : ((uint32_t)p[i + 1] << 8 | p[i]);
            if (u == 0) { break; }
            if (u >= 0xD800 && u <= 0xDBFF && i + 3 < n)   /* surrogate pair */
            {
                uint32_t lo = be ? ((uint32_t)p[i + 2] << 8 | p[i + 3])
                                 : ((uint32_t)p[i + 3] << 8 | p[i + 2]);
                if (lo >= 0xDC00 && lo <= 0xDFFF)
                {
                    u = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u);
                    i += 2;
                }
            }
            pos = mp_utf8_put(dst, cap - 1, pos, u);
        }
    }
    dst[pos] = '\0';
}

/* Remove ID3 unsynchronisation (each 0xFF 0x00 sequence -> 0xFF) in place and
 * return the new length. Harmless on data that was never unsynchronised. */
static uint32_t mp_id3_deunsync(uint8_t *p, uint32_t n)
{
    uint32_t r = 0;
    uint32_t w = 0;

    while (r < n)
    {
        p[w++] = p[r];
        if (p[r] == 0xFF && r + 1 < n && p[r + 1] == 0x00) { r += 2; }
        else                                               { r += 1; }
    }
    return w;
}

/* Read the MP3 ID3v2 TIT2 (title) / TPE1 (artist) frames and decode them to
 * UTF-8 ourselves. Empty output means the frame was absent. Handles ID3v2.3/2.4
 * plus unsynchronisation, extended header and v2.4 data-length indicators, since
 * taggers (e.g. Mp3tag) emit these and the SDK parser mishandles them. */
static void mp_read_id3v2_mp3(const char *path, char *title, int tcap, char *artist, int acap)
{
    uint8_t  hdr[10];
    uint8_t *buf;
    uint32_t tag_size;
    uint32_t off;
    uint32_t start;
    uint8_t  ver;
    uint8_t  flags;
    int      fd;

    if (title != NULL && tcap > 0)  { title[0] = '\0'; }
    if (artist != NULL && acap > 0) { artist[0] = '\0'; }

    fd = bk_vfs_open(path, O_RDONLY);
    if (fd < 0) { return; }

    if (bk_vfs_read(fd, hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3')
    {
        bk_vfs_close(fd);
        return;
    }

    ver   = hdr[3];
    flags = hdr[5];
    tag_size = ((uint32_t)(hdr[6] & 0x7F) << 21) | ((uint32_t)(hdr[7] & 0x7F) << 14) |
               ((uint32_t)(hdr[8] & 0x7F) << 7)  | (uint32_t)(hdr[9] & 0x7F);
    if (tag_size == 0 || tag_size > (1u << 20))
    {
        bk_vfs_close(fd);
        return;
    }

    buf = (uint8_t *)psram_malloc(tag_size);
    if (buf == NULL)
    {
        bk_vfs_close(fd);
        return;
    }
    if ((uint32_t)bk_vfs_read(fd, buf, tag_size) != tag_size)
    {
        os_free(buf);
        bk_vfs_close(fd);
        return;
    }
    bk_vfs_close(fd);

    /* Whole-tag unsynchronisation (ID3v2.2/2.3 apply it to the entire body). */
    if (ver < 4 && (flags & 0x80))
    {
        tag_size = mp_id3_deunsync(buf, tag_size);
    }

    /* Skip an extended header when present, so frame parsing starts on a frame. */
    start = 0;
    if ((flags & 0x40) && tag_size >= 4)
    {
        uint32_t ext;

        if (ver == 4)   /* v2.4: synchsafe size that already includes itself */
        {
            ext = ((uint32_t)(buf[0] & 0x7F) << 21) | ((uint32_t)(buf[1] & 0x7F) << 14) |
                  ((uint32_t)(buf[2] & 0x7F) << 7)  | (uint32_t)(buf[3] & 0x7F);
            start = ext;
        }
        else            /* v2.3: plain size that excludes its own 4 size bytes */
        {
            ext = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                  ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
            start = 4 + ext;
        }
        if (start >= tag_size) { start = 0; }   /* corrupt -> ignore */
    }

    off = start;
    while (off + 10 <= tag_size)
    {
        uint8_t *fh = buf + off;
        uint8_t *fdata;
        uint32_t fsz;
        uint32_t fdlen;
        uint8_t  fflags;

        if (fh[0] == 0) { break; }   /* padding */

        if (ver == 4)
        {
            fsz = ((uint32_t)(fh[4] & 0x7F) << 21) | ((uint32_t)(fh[5] & 0x7F) << 14) |
                  ((uint32_t)(fh[6] & 0x7F) << 7)  | (uint32_t)(fh[7] & 0x7F);
        }
        else
        {
            fsz = ((uint32_t)fh[4] << 24) | ((uint32_t)fh[5] << 16) |
                  ((uint32_t)fh[6] << 8)  | (uint32_t)fh[7];
        }
        if (fsz == 0 || off + 10 + fsz > tag_size) { break; }

        fflags = fh[9];        /* status/format flags live in the 2nd flag byte */
        fdata  = fh + 10;
        fdlen  = fsz;

        if (ver == 4 && (fflags & 0x01) && fdlen >= 4)   /* data-length indicator */
        {
            fdata += 4;
            fdlen -= 4;
        }
        if (ver == 4 && (fflags & 0x02))                 /* per-frame unsync */
        {
            fdlen = mp_id3_deunsync(fdata, fdlen);
        }

        if (memcmp(fh, "TIT2", 4) == 0 && title != NULL)
        {
            mp_id3_text_to_utf8(fdata, fdlen, title, tcap);
        }
        else if (memcmp(fh, "TPE1", 4) == 0 && artist != NULL)
        {
            mp_id3_text_to_utf8(fdata, fdlen, artist, acap);
        }

        off += 10 + fsz;
    }

    os_free(buf);
}

/* Overlay embedded tag metadata (title/artist/duration) onto a track that has
 * already been filled from its filename. Tag values only win when they are a
 * real tag (not the parser's path fallback) and valid UTF-8, so the filename
 * split stays as a robust fallback. */
static void mp_read_tags(const char *name, mp_track_t *t)
{
    static audio_metadata_t meta;   /* ~2KB; scan is sequential, not reentrant */
    const bk_audio_player_metadata_parser_ops_t *ops = mp_parser_for(name);
    char base_noext[MP_TITLE_MAX + MP_ARTIST_MAX];
    char *dot;
    int fd;

    if (ops == NULL || ops->parse == NULL)
    {
        return;
    }

    fd = bk_vfs_open(t->path, O_RDONLY);
    if (fd < 0)
    {
        /* Chinese long names get truncated to GBK 8.3 short names under
         * FF_CODE_PAGE=437, and some GBK bytes get mangled by the CP437 upper
         * -case table so the path no longer resolves. Flag it instead of
         * silently dropping the track. */
        LOGE("open failed (rename to ASCII): '%s' fd=%d\n", t->path, fd);
        return;
    }

    memset(&meta, 0, sizeof(meta));
    if (ops->parse(fd, t->path, &meta) == 0)
    {
        /* basename without extension = the parser's path-derived title fallback */
        snprintf(base_noext, sizeof(base_noext), "%s", name);
        dot = strrchr(base_noext, '.');
        if (dot != NULL) { *dot = '\0'; }

        if (meta.title[0] != '\0' && strcmp(meta.title, base_noext) != 0 &&
            mp_is_valid_utf8(meta.title))
        {
            snprintf(t->title, sizeof(t->title), "%s", meta.title);
        }
        if (meta.artist[0] != '\0' && mp_is_valid_utf8(meta.artist))
        {
            snprintf(t->artist, sizeof(t->artist), "%s", meta.artist);
        }
        if (meta.duration > 0.0)
        {
            t->dur_ms = (uint32_t)meta.duration;   /* parsers report milliseconds */
        }
    }

    bk_vfs_close(fd);

    /* The SDK ID3v2 reader mangles UTF-16 text (the encoding these files use),
     * so re-read TIT2/TPE1 ourselves with correct UTF-16 -> UTF-8 decoding and
     * prefer that when valid. */
    if (mp_ends_with(name, ".mp3"))
    {
        char id_title[MP_TITLE_MAX];
        char id_artist[MP_ARTIST_MAX];

        mp_read_id3v2_mp3(t->path, id_title, sizeof(id_title), id_artist, sizeof(id_artist));

        if (id_title[0] != '\0' && mp_is_valid_utf8(id_title))
        {
            snprintf(t->title, sizeof(t->title), "%s", id_title);
        }
        if (id_artist[0] != '\0' && mp_is_valid_utf8(id_artist))
        {
            snprintf(t->artist, sizeof(t->artist), "%s", id_artist);
        }
    }
}

static void mp_scan_tracks(void)
{
    DIR *dir;
    struct dirent *entry;

    s_track_cnt = 0;

    if (boot_sd_mount() != BK_OK)
    {
        LOGI("SD not mounted, playlist empty\n");
        return;
    }

    dir = bk_vfs_opendir(MP_MUSIC_DIR);
    if (dir == NULL)
    {
        LOGI("opendir %s failed\n", MP_MUSIC_DIR);
        return;
    }

    while ((entry = bk_vfs_readdir(dir)) != NULL && s_track_cnt < MP_MAX_TRACKS)
    {
        mp_track_t *t;

        if (!mp_has_audio_suffix(entry->d_name))
        {
            continue;
        }

        t = &s_tracks[s_track_cnt];
        t->dur_ms = 0;
        mp_parse_name(entry->d_name, t);
        snprintf(t->path, sizeof(t->path), "%s/%s", MP_MUSIC_DIR, entry->d_name);
        mp_read_tags(entry->d_name, t);
        s_track_cnt++;
    }

    bk_vfs_closedir(dir);
    LOGI("scanned %d track(s) in %s\n", s_track_cnt, MP_MUSIC_DIR);
}

/* ------------------------------------------------------------------ */
/* Playlist rows                                                      */
/* ------------------------------------------------------------------ */

static void mp_reset_list(lv_obj_t *list)
{
    lv_obj_clean(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
}

static void mp_add_empty(lv_obj_t *list, const char *text)
{
    lv_obj_t *lbl = lv_label_create(list);

    mp_set_font(lbl, s_cn_24);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x9FB3C8),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(lbl, 24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(lbl, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, text);
}

static void mp_add_track_row(lv_obj_t *list, int idx, const mp_track_t *t)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, MP_ROW_W, MP_ROW_H);
    lv_obj_set_style_bg_opa(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(row, 26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ix = lv_label_create(row);
    char idxbuf[8];
    snprintf(idxbuf, sizeof(idxbuf), "%d", idx + 1);
    mp_set_font(ix, s_cn_24);
    lv_obj_set_style_text_color(ix, lv_color_hex(0x9FB3C8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ix, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ix, 14, 36);
    lv_obj_set_width(ix, 32);
    lv_label_set_text(ix, idxbuf);

    lv_obj_t *ti = lv_label_create(row);
    mp_set_font(ti, s_cn_30);
    lv_obj_set_style_text_color(ti, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ti, 52, 16);
    lv_obj_set_width(ti, 268);
    lv_label_set_long_mode(ti, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(ti, t->title);

    lv_obj_t *ar = lv_label_create(row);
    mp_set_font(ar, s_cn_24);
    lv_obj_set_style_text_color(ar, lv_color_hex(0x9FB3C8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ar, 52, 58);
    lv_obj_set_width(ar, 268);
    lv_label_set_long_mode(ar, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(ar, t->artist[0] != '\0' ? t->artist : "Unknown");

    lv_obj_t *du = lv_label_create(row);
    char durbuf[8];
    if (t->dur_ms > 0)
    {
        uint32_t sec = t->dur_ms / 1000u;
        snprintf(durbuf, sizeof(durbuf), "%u:%02u", (unsigned)(sec / 60u), (unsigned)(sec % 60u));
    }
    else
    {
        snprintf(durbuf, sizeof(durbuf), "--:--");
    }
    mp_set_font(du, s_cn_24);
    lv_obj_set_style_text_color(du, lv_color_hex(0x9FB3C8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(du, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(du, 328, 36);
    lv_obj_set_width(du, 80);
    lv_label_set_text(du, durbuf);
}

static void mp_fill_playlist(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *list = ui->music_player_pl_list;

    if (list == NULL || !lv_obj_is_valid(list))
    {
        return;
    }

    mp_reset_list(list);

    for (int i = 0; i < s_track_cnt; i++)
    {
        mp_add_track_row(list, i, &s_tracks[i]);
    }
    s_pl_rows = s_track_cnt;

    if (s_track_cnt == 0)
    {
        mp_add_empty(list, boot_sd_is_mounted() ? "No music found" : "No SD card");
    }

    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);

    if (ui->music_player_pl_count != NULL && lv_obj_is_valid(ui->music_player_pl_count))
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "共 %d 首", s_track_cnt);
        lv_label_set_text(ui->music_player_pl_count, buf);
    }
    LOGI("built %d playlist rows\n", s_track_cnt);
}

/* ------------------------------------------------------------------ */
/* Key-driven focus + row selection                                   */
/* ------------------------------------------------------------------ */

static void mp_apply_selection(lv_obj_t *list, int sel, bool focused)
{
    uint32_t cnt;
    uint32_t i;

    if (list == NULL || !lv_obj_is_valid(list))
    {
        return;
    }

    cnt = lv_obj_get_child_count(list);
    for (i = 0; i < cnt; i++)
    {
        lv_obj_t *row = lv_obj_get_child(list, i);

        if (row == NULL || !lv_obj_is_valid(row))
        {
            continue;
        }

        if (focused && (int)i == sel)
        {
            lv_obj_set_style_bg_color(row, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(row, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_scroll_to_view(row, LV_ANIM_ON);
        }
        else
        {
            lv_obj_set_style_bg_opa(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/* Mark a panel as focused with an accent outline (leaves the designed border
 * untouched). */
static void mp_mark_focus(lv_obj_t *panel, bool focused)
{
    if (panel == NULL || !lv_obj_is_valid(panel))
    {
        return;
    }

    lv_obj_set_style_outline_width(panel, focused ? 3 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(panel, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(panel, focused ? 255 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void mp_clamp_sel(void)
{
    if (s_pl_sel < 0 || s_pl_sel >= s_pl_rows)
    {
        s_pl_sel = 0;
    }
}

/* np-panel control buttons, in single-press cycling order. */
static lv_obj_t *mp_np_btn(int i)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    switch (i)
    {
    case 0:  return ui->music_player_btn_mode;
    case 1:  return ui->music_player_btn_prev;
    case 2:  return ui->music_player_btn_play;
    case 3:  return ui->music_player_btn_next;
    case 4:  return ui->music_player_btn_vol_down;
    case 5:  return ui->music_player_btn_vol_up;
    default: return NULL;
    }
}

/* Highlight the selected np-panel button (outline), clear the rest. */
static void mp_np_mark_btn(int sel, bool focused)
{
    for (int i = 0; i < MP_NP_BTN_CNT; i++)
    {
        lv_obj_t *b = mp_np_btn(i);
        bool on = focused && i == sel;

        if (b == NULL || !lv_obj_is_valid(b))
        {
            continue;
        }
        lv_obj_set_style_outline_width(b, on ? 3 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_color(b, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_opa(b, on ? 255 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_pad(b, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void mp_refresh_focus(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    mp_mark_focus(ui->music_player_np_panel, s_focus == MP_FOCUS_NP);
    mp_mark_focus(ui->music_player_pl_list, s_focus == MP_FOCUS_PL);

    mp_apply_selection(ui->music_player_pl_list, s_pl_sel, s_focus == MP_FOCUS_PL);
    mp_np_mark_btn(s_np_sel, s_focus == MP_FOCUS_NP);
}

static void mp_set_focus(mp_focus_t f)
{
    s_focus = f;
    if (f == MP_FOCUS_PL)
    {
        mp_clamp_sel();
    }
    else if (f == MP_FOCUS_NP)
    {
        s_np_sel = 0;   /* start cycling from the first button */
    }
    mp_refresh_focus();
    LOGI("focus -> %d\n", (int)f);
}

static bool mp_is_active(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    return ui->music_player != NULL && lv_obj_is_valid(ui->music_player) &&
           ui->music_player == lv_screen_active();
}

/* ------------------------------------------------------------------ */
/* Playback (bk_audio_player)                                         */
/* ------------------------------------------------------------------ */

static void mp_fmt_time(uint32_t sec, char *buf, int cap)
{
    snprintf(buf, cap, "%u:%02u", (unsigned)(sec / 60u), (unsigned)(sec % 60u));
}

/* Push now-playing metadata into the np_panel. LVGL thread only. */
static void mp_now_playing_apply(int idx)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    const mp_track_t *t;
    char buf[16];
    uint32_t dur;

    if (idx < 0 || idx >= s_track_cnt || !mp_is_active())
    {
        return;
    }
    t = &s_tracks[idx];

    if (ui->music_player_song_title != NULL && lv_obj_is_valid(ui->music_player_song_title))
    {
        lv_label_set_text(ui->music_player_song_title, t->title);
    }
    if (ui->music_player_song_artist != NULL && lv_obj_is_valid(ui->music_player_song_artist))
    {
        lv_label_set_text(ui->music_player_song_artist, t->artist[0] != '\0' ? t->artist : "Unknown");
    }

    dur = t->dur_ms / 1000u;
    if (ui->music_player_total_time != NULL && lv_obj_is_valid(ui->music_player_total_time))
    {
        mp_fmt_time(dur, buf, sizeof(buf));
        lv_label_set_text(ui->music_player_total_time, buf);
    }
    if (ui->music_player_cur_time != NULL && lv_obj_is_valid(ui->music_player_cur_time))
    {
        lv_label_set_text(ui->music_player_cur_time, "0:00");
    }
    if (ui->music_player_progress != NULL && lv_obj_is_valid(ui->music_player_progress))
    {
        lv_bar_set_range(ui->music_player_progress, 0, dur > 0 ? (int)dur : 1);
        lv_bar_set_value(ui->music_player_progress, 0, LV_ANIM_OFF);
    }
}

/* Reflect the current play mode on the mode button icon. LVGL thread only. */
static void mp_mode_icon_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    const void *src;

    if (ui->music_player_btn_mode == NULL || !lv_obj_is_valid(ui->music_player_btn_mode))
    {
        return;
    }
    switch (s_play_mode)
    {
    case AUDIO_PLAYER_MODE_ONE_SONG_LOOP: src = &repeat_one_sm_52x52_RGB565A8_NONE; break;
    case AUDIO_PLAYER_MODE_RANDOM:        src = &shuffle_sm_52x52_RGB565A8_NONE;    break;
    default:                              src = &repeat_sm_52x52_RGB565A8_NONE;      break;
    }
    lv_image_set_src(ui->music_player_btn_mode, src);
}

/* Reflect the play/pause state on the center button icon. LVGL thread only. */
static void mp_play_icon_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (ui->music_player_btn_play == NULL || !lv_obj_is_valid(ui->music_player_btn_play))
    {
        return;
    }
    lv_image_set_src(ui->music_player_btn_play,
                     (s_pb == MP_PB_PLAYING) ? (const void *)&music_pause_sm_72x72_RGB565A8_NONE
                               : (const void *)&music_play_sm_72x72_RGB565A8_NONE);
}

/* --- async trampolines: player events fire on the player task, LVGL must be
 *     touched on the LVGL task, so hop through lv_async_call. --- */
static void mp_async_start(void *unused)
{
    (void)unused;

    /* This trampoline is queued from SONG_START but runs later on the LVGL
     * task. By the time it fires the page may have been left and the pipeline
     * torn down (bk_audio_player_stop -> priv->sink == NULL). Bail out unless
     * we are still the active, playing page so we never touch a dead sink. */
    if (!mp_is_active() || s_pb != MP_PB_PLAYING || s_player == NULL)
    {
        return;
    }

    mp_now_playing_apply(s_now_idx);
    mp_play_icon_apply();

    /* Deliberately do NOT call bk_audio_player_set_volume() here: its 0..100
     * range maps to 0..63 dB of DAC *digital gain* (amplify-only, min 0 dB),
     * which is louder than the pipeline's default (-7 dB) and, being a cross-
     * core unsynchronized call into play_sm_set_volume(), also raced a NULL
     * sink and faulted. Leave the pipeline at its built-in gain instead. */
}

static void mp_async_tick(void *unused)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    char buf[16];

    (void)unused;
    if (!mp_is_active())
    {
        return;
    }
    if (ui->music_player_cur_time != NULL && lv_obj_is_valid(ui->music_player_cur_time))
    {
        mp_fmt_time((uint32_t)s_tick_sec, buf, sizeof(buf));
        lv_label_set_text(ui->music_player_cur_time, buf);
    }
    if (ui->music_player_progress != NULL && lv_obj_is_valid(ui->music_player_progress))
    {
        lv_bar_set_value(ui->music_player_progress, s_tick_sec, LV_ANIM_OFF);
    }
}

static void mp_async_icon(void *unused)
{
    (void)unused;
    mp_play_icon_apply();
}

static void mp_player_event_cb(audio_player_event_type_t event, void *extra_info, void *args)
{
    (void)args;

    switch (event)
    {
    case AUDIO_PLAYER_EVENT_SONG_START:
        s_pb = MP_PB_PLAYING;
        lv_async_call(mp_async_start, NULL);
        break;

    case AUDIO_PLAYER_EVENT_SONG_TICK:
        s_tick_sec = (extra_info != NULL) ? *(int *)extra_info : 0;
        lv_async_call(mp_async_tick, NULL);
        break;

    case AUDIO_PLAYER_EVENT_SONG_PAUSE:
        s_pb = MP_PB_PAUSED;
        lv_async_call(mp_async_icon, NULL);
        break;

    case AUDIO_PLAYER_EVENT_SONG_RESUME:
        s_pb = MP_PB_PLAYING;
        lv_async_call(mp_async_icon, NULL);
        break;

    case AUDIO_PLAYER_EVENT_SONG_FINISH:
        /* Keep our local index in lock-step with the player's auto-advance so
         * the next SONG_START shows the right metadata (list order == ours).
         * Only SEQUENCE(_LOOP) steps forward; ONE_SONG_LOOP replays the same
         * index. RANDOM jumps to an index we cannot predict, so leave it. */
        if (s_track_cnt > 0 &&
            (s_play_mode == AUDIO_PLAYER_MODE_SEQUENCE ||
             s_play_mode == AUDIO_PLAYER_MODE_SEQUENCE_LOOP))
        {
            s_now_idx = (s_now_idx + 1) % s_track_cnt;
        }
        break;

    default:
        break;
    }
}

/* (Re)load the scanned track list into the player so player indices match our
 * playlist rows. Safe to call whenever the instance exists. */
static void mp_player_sync_list(void)
{
    if (s_player == NULL)
    {
        return;
    }
    bk_audio_player_clear_music_list(s_player);
    for (int i = 0; i < s_track_cnt; i++)
    {
        bk_audio_player_add_music(s_player, s_tracks[i].title, s_tracks[i].path);
    }
}

/* Lazily create the player, register plugins and load the current playlist. */
static bk_audio_player_handle_t mp_player_ensure(void)
{
    bk_audio_player_cfg_t cfg = DEFAULT_AUDIO_PLAYER_CONFIG();

    if (s_player != NULL)
    {
        return s_player;
    }

    cfg.event_handler = mp_player_event_cb;
    cfg.args = NULL;
    if (bk_audio_player_new(&s_player, &cfg) != AUDIO_PLAYER_OK || s_player == NULL)
    {
        s_player = NULL;
        LOGI("audio player create failed\n");
        return NULL;
    }

    bk_audio_player_register_source(s_player, bk_audio_player_get_file_source_ops());
    bk_audio_player_register_sink(s_player, bk_audio_player_get_onboard_speaker_sink_ops());
    bk_audio_player_register_decoder(s_player, bk_audio_player_get_mp3_decoder_ops());
    bk_audio_player_register_decoder(s_player, bk_audio_player_get_wav_decoder_ops());
    bk_audio_player_register_decoder(s_player, bk_audio_player_get_aac_decoder_ops());

    bk_audio_player_set_play_mode(s_player, AUDIO_PLAYER_MODE_SEQUENCE_LOOP);
    /* Volume is applied on the first SONG_START (see mp_async_start): the sink
     * that set_volume touches does not exist until playback begins. */

    {
        bk_audio_player_pa_ctrl_t pa = DEFAULT_AUDIO_PLAYER_PA_CTRL();

        pa.pa_ctrl_en   = true;
        pa.pa_ctrl_gpio = 13;
        pa.pa_on_level  = 1;
        (void)bk_audio_player_set_pa_ctrl(s_player, &pa);
    }

    mp_player_sync_list();
    return s_player;
}

/* ------------------------------------------------------------------ */
/* Physical-key handlers                                              */
/* ------------------------------------------------------------------ */

bool music_player_ui_handle_key_double(void)
{
    mp_focus_t next;

    if (!mp_is_active())
    {
        return false;
    }

    /* whole-page -> now-playing -> playlist -> whole-page */
    next = (s_focus >= MP_FOCUS_PL) ? MP_FOCUS_NONE : (mp_focus_t)(s_focus + 1);
    mp_set_focus(next);
    return true;
}

bool music_player_ui_handle_key_single(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (!mp_is_active() || s_focus == MP_FOCUS_NONE)
    {
        /* Let the home-menu single-press behavior (return home) run. */
        return false;
    }

    if (s_focus == MP_FOCUS_PL)
    {
        if (s_pl_rows <= 0)
        {
            return true;
        }
        s_pl_sel = (s_pl_sel + 1) % s_pl_rows;
        mp_apply_selection(ui->music_player_pl_list, s_pl_sel, true);
        LOGI("single -> sel=%d\n", s_pl_sel);
    }
    else if (s_focus == MP_FOCUS_NP)
    {
        /* Cycle the highlight across the np-panel control buttons. */
        s_np_sel = (s_np_sel + 1) % MP_NP_BTN_CNT;
        mp_np_mark_btn(s_np_sel, true);
        LOGI("single -> np_btn=%d\n", s_np_sel);
    }

    /* NP focus (or PL) consumes the press so it does not return home. */
    return true;
}

/* Trigger the action of the currently highlighted np-panel button. Runs on the
 * key/LVGL task, same context as the playlist long-press play. */
static void mp_volume_adjust(int delta)
{
    lv_obj_t *slider = bk_lv_tool_ui.music_player_vol_slider;
    int value;

    if (slider == NULL || !lv_obj_is_valid(slider))
    {
        return;
    }

    value = lv_slider_get_value(slider) + delta;
    if (value < 0)
    {
        value = 0;
    }
    else if (value > 100)
    {
        value = 100;
    }

    lv_slider_set_value(slider, value, LV_ANIM_ON);
    LOGI("volume slider -> %d\n", value);
}

static void mp_volume_btn_clicked(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (target == ui->music_player_btn_vol_down)
    {
        mp_volume_adjust(-MP_VOLUME_STEP);
    }
    else if (target == ui->music_player_btn_vol_up)
    {
        mp_volume_adjust(MP_VOLUME_STEP);
    }
}

static void mp_np_activate(int sel)
{
    if (sel == 4 || sel == 5)
    {
        mp_volume_adjust(sel == 4 ? -MP_VOLUME_STEP : MP_VOLUME_STEP);
        return;
    }

    if (mp_player_ensure() == NULL)
    {
        return;
    }

    switch (sel)
    {
    case 0:  /* btn_mode: cycle play mode */
        s_play_mode = (s_play_mode == AUDIO_PLAYER_MODE_SEQUENCE_LOOP)  ? AUDIO_PLAYER_MODE_ONE_SONG_LOOP :
                      (s_play_mode == AUDIO_PLAYER_MODE_ONE_SONG_LOOP)  ? AUDIO_PLAYER_MODE_RANDOM :
                                                                          AUDIO_PLAYER_MODE_SEQUENCE_LOOP;
        bk_audio_player_set_play_mode(s_player, s_play_mode);
        mp_mode_icon_apply();
        LOGI("np: play mode -> %d\n", (int)s_play_mode);
        break;

    case 1:  /* btn_prev */
        bk_audio_player_prev(s_player);
        /* Player advances in list order (== our s_tracks order); keep s_now_idx
         * in step so the SONG_START trampoline shows the right metadata. Mirrors
         * the sequential assumption already used on SONG_FINISH. */
        if (s_track_cnt > 0)
        {
            s_now_idx = (s_now_idx - 1 + s_track_cnt) % s_track_cnt;
        }
        LOGI("np: prev -> %d\n", s_now_idx);
        break;

    case 2:  /* btn_play: playing -> pause, paused -> resume, stopped -> start */
        if (s_pb == MP_PB_PLAYING)
        {
            bk_audio_player_pause(s_player);
            LOGI("np: pause\n");
        }
        else if (s_pb == MP_PB_PAUSED)
        {
            bk_audio_player_resume(s_player);
            LOGI("np: resume\n");
        }
        else if (s_track_cnt > 0)
        {
            /* Nothing playing yet (page just opened / playback stopped): start
             * the selected track. resume() on a STOPPED pipeline is a no-op. */
            int idx = (s_pl_sel >= 0 && s_pl_sel < s_track_cnt) ? s_pl_sel : 0;

            if (bk_audio_player_jumpto(s_player, idx) == AUDIO_PLAYER_OK)
            {
                s_now_idx = idx;
                s_pb      = MP_PB_PLAYING;
                mp_now_playing_apply(idx);
                mp_play_icon_apply();
                LOGI("np: start track %d\n", idx);
            }
        }
        break;

    case 3:  /* btn_next */
        bk_audio_player_next(s_player);
        if (s_track_cnt > 0)
        {
            s_now_idx = (s_now_idx + 1) % s_track_cnt;
        }
        LOGI("np: next -> %d\n", s_now_idx);
        break;

    default:
        break;
    }
}

bool music_player_ui_handle_key_long(void)
{
    if (!mp_is_active())
    {
        return false;
    }

    /* Long-press on the now-playing panel triggers the highlighted control
     * button; stay on the page. */
    if (s_focus == MP_FOCUS_NP)
    {
        mp_np_activate(s_np_sel);
        return true;
    }

    /* Long-press on a playlist row plays the selected track and stays on the
     * page. Any other focus lets the long press bubble up (return home). */
    if (s_focus == MP_FOCUS_PL && s_pl_rows > 0)
    {
        if (mp_player_ensure() == NULL)
        {
            return true;
        }
        if (bk_audio_player_jumpto(s_player, s_pl_sel) == AUDIO_PLAYER_OK)
        {
            s_now_idx = s_pl_sel;
            s_pb      = MP_PB_PLAYING;
            mp_now_playing_apply(s_pl_sel);
            mp_play_icon_apply();
            LOGI("play track %d\n", s_pl_sel);
        }
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void music_player_ui_enter(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (ui->music_player == NULL || !lv_obj_is_valid(ui->music_player))
    {
        return;
    }

    s_focus  = MP_FOCUS_NONE;
    s_pl_sel = 0;

    mp_fonts_ensure();

    /* Now-playing metadata (may contain Chinese). */
    mp_set_font(ui->music_player_song_title, s_cn_30);
    mp_set_font(ui->music_player_song_artist, s_cn_24);
    mp_set_font(ui->music_player_pl_count, s_cn_24);

    mp_scan_tracks();
    mp_fill_playlist();
    mp_refresh_focus();

    if (!s_volume_btns_bound &&
        ui->music_player_btn_vol_down != NULL &&
        ui->music_player_btn_vol_up != NULL &&
        lv_obj_is_valid(ui->music_player_btn_vol_down) &&
        lv_obj_is_valid(ui->music_player_btn_vol_up))
    {
        lv_obj_add_flag(ui->music_player_btn_vol_down, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(ui->music_player_btn_vol_up, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ui->music_player_btn_vol_down,
                            mp_volume_btn_clicked,
                            LV_EVENT_CLICKED,
                            NULL);
        lv_obj_add_event_cb(ui->music_player_btn_vol_up,
                            mp_volume_btn_clicked,
                            LV_EVENT_CLICKED,
                            NULL);
        s_volume_btns_bound = true;
    }

    /* Keep the player's playlist aligned with the freshly scanned rows so a
     * later jumpto(idx) targets the same track the UI shows. */
    mp_player_sync_list();
}

void music_player_ui_leave(void)
{
    s_focus = MP_FOCUS_NONE;

    /* Stop audio when leaving the page; keep the instance for fast re-entry. */
    if (s_player != NULL)
    {
        bk_audio_player_stop(s_player);
        s_pb      = MP_PB_STOPPED;
        s_now_idx = -1;
    }
}
