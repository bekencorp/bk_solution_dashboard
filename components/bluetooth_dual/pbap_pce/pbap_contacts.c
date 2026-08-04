/**
 * @file pbap_contacts.c
 *
 * PBAP-based contact cache used to resolve an incoming call number to a name.
 * See pbap_contacts.h for the public flow.
 *
 * Threading:
 *   - PBAP PCE callbacks run in the BT stack context; they only copy the payload
 *     and post a message to s_evt_queue (never block the stack).
 *   - s_evt_thread drains the queue, parses vCard data and fills the cache.
 *   - pbap_contacts_lookup() runs in the HFP callback (BT stack) context.
 *   The cache is shared between the worker (write) and lookup (read) contexts,
 *   so it is guarded by s_cache_lock.
 */

#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "pbap_contacts.h"
#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_pbap_pce.h"
#include "bt_manager.h"

#define TAG "pbap_contacts"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* -------------------------------------------------------------------------- */
/* Tunables                                                                    */
/* -------------------------------------------------------------------------- */

#define PBAP_CONTACTS_MAX      500          /* Max cached contacts (PSRAM bound) */
#define PBAP_NAME_MAX          40           /* Display name (UTF-8; ~13 CJK) */
#define PBAP_KEY_DIGITS        9            /* Compare last N digits only */
#define PBAP_TEL_RAW_MAX       32           /* Raw TEL value length */
#define PBAP_CARD_TEL_MAX      3            /* TELs kept per vCard */

#define PBAP_PB_OBJECT_NAME    "telecom/pb.vcf"
#define PBAP_CONNECT_DELAY_MS  1500         /* Let SLC/authorization settle first */

#define PBAP_EVT_QUEUE_LEN     64
#define PBAP_EVT_TASK_STACK    (1024 * 6)
#define PBAP_EVT_TASK_PRIO     5

/* vCard body chunk is <= BK_PBAP_PCE_DEFAULT_MAX_RECV_SIZE; keep a small tail for
 * lines split across chunks. */
#define PBAP_TAIL_MAX          256
#define PBAP_COMBINED_MAX      (BK_PBAP_PCE_DEFAULT_MAX_RECV_SIZE + PBAP_TAIL_MAX)

/* Internal queue message kinds (reuse the PCE event enum space). */
#define PBAP_MSG_CONNECT_REQ   ((bk_pbap_pce_cb_event_t)0xFD)
#define PBAP_MSG_EXIT          ((bk_pbap_pce_cb_event_t)0xFE)

/* -------------------------------------------------------------------------- */
/* Types                                                                       */
/* -------------------------------------------------------------------------- */

typedef struct {
    char key[PBAP_KEY_DIGITS + 1];   /* normalized (last N digits) */
    char name[PBAP_NAME_MAX];        /* FN preferred, else N */
} pbap_contact_t;

typedef struct {
    bk_pbap_pce_cb_event_t event;
    bk_pbap_pce_status_t   status;
    uint8_t                bd_addr[BK_BD_ADDR_LEN];
    uint16_t               data_len;
    uint8_t               *data_ptr;   /* malloc'd in callback, freed by worker */
} pbap_msg_t;

/* -------------------------------------------------------------------------- */
/* State                                                                       */
/* -------------------------------------------------------------------------- */

static beken_queue_t   s_evt_queue;
static beken_thread_t  s_evt_thread;
static beken_mutex_t   s_cache_lock;

static pbap_contact_t *s_contacts;        /* PSRAM array [PBAP_CONTACTS_MAX] */
static uint16_t        s_contact_cnt;
static uint8_t         s_cached_addr[BK_BD_ADDR_LEN];  /* addr the cache belongs to */
static uint8_t         s_cache_valid;

/* Pending connect target (set on ACL up, checked by worker). */
static uint8_t         s_target_addr[BK_BD_ADDR_LEN];
static uint8_t         s_want_connect;
static uint8_t         s_inited;
static uint8_t         s_bt_manager_index = 0xFF;  /* bt_manager gap callback slot */

/* Per-pull parser state (worker context only). */
static char     s_tail[PBAP_TAIL_MAX];
static size_t   s_tail_len;
static char     s_combined[PBAP_COMBINED_MAX];
static char     s_cur_name[PBAP_NAME_MAX];
static char     s_cur_tel[PBAP_CARD_TEL_MAX][PBAP_TEL_RAW_MAX];
static uint8_t  s_cur_tel_cnt;

/* Lookup result buffer (single caller: HFP CLIP handler). */
static char     s_lookup_buf[PBAP_NAME_MAX];

/* -------------------------------------------------------------------------- */
/* Number normalization + cache                                                */
/* -------------------------------------------------------------------------- */

/* Keep only digits, then take the last PBAP_KEY_DIGITS of them. */
static void normalize_number(const char *in, char *out /* [PBAP_KEY_DIGITS+1] */)
{
    char digits[48];
    int n = 0;

    out[0] = '\0';
    if (!in) {
        return;
    }
    for (const char *p = in; *p && n < (int)sizeof(digits) - 1; p++) {
        if (*p >= '0' && *p <= '9') {
            digits[n++] = *p;
        }
    }
    digits[n] = '\0';
    int start = (n > PBAP_KEY_DIGITS) ? (n - PBAP_KEY_DIGITS) : 0;
    os_strncpy(out, digits + start, PBAP_KEY_DIGITS);
    out[PBAP_KEY_DIGITS] = '\0';
}

static void cache_clear_locked(void)
{
    s_contact_cnt = 0;
    s_cache_valid = 0;
}

static void cache_add(const char *name, const char *tel)
{
    char key[PBAP_KEY_DIGITS + 1];

    normalize_number(tel, key);
    if (key[0] == '\0') {
        return;
    }

    rtos_lock_mutex(&s_cache_lock);
    if (s_contact_cnt < PBAP_CONTACTS_MAX) {
        pbap_contact_t *c = &s_contacts[s_contact_cnt++];
        os_strncpy(c->key, key, PBAP_KEY_DIGITS);
        c->key[PBAP_KEY_DIGITS] = '\0';
        snprintf(c->name, sizeof(c->name), "%s", (name && name[0]) ? name : tel);
    }
    rtos_unlock_mutex(&s_cache_lock);
}

const char *pbap_contacts_lookup(const char *number)
{
    char key[PBAP_KEY_DIGITS + 1];
    const char *result = NULL;

    if (!s_inited || !s_contacts || !number || !number[0]) {
        return NULL;
    }

    normalize_number(number, key);
    if (key[0] == '\0') {
        return NULL;
    }

    rtos_lock_mutex(&s_cache_lock);
    if (s_cache_valid) {
        for (uint16_t i = 0; i < s_contact_cnt; i++) {
            if (os_strcmp(s_contacts[i].key, key) == 0) {
                snprintf(s_lookup_buf, sizeof(s_lookup_buf), "%s", s_contacts[i].name);
                result = s_lookup_buf;
                break;
            }
        }
    }
    rtos_unlock_mutex(&s_cache_lock);

    return result;
}

/* -------------------------------------------------------------------------- */
/* vCard parsing (worker context)                                              */
/* -------------------------------------------------------------------------- */

static void parser_card_reset(void)
{
    s_cur_name[0] = '\0';
    s_cur_tel_cnt = 0;
}

static void parser_reset(void)
{
    s_tail_len = 0;
    parser_card_reset();
}

/* Case-insensitive compare of a vCard property type token. */
static int type_is(const char *p, size_t len, const char *type)
{
    size_t tlen = os_strlen(type);
    if (len != tlen) {
        return 0;
    }
    for (size_t i = 0; i < tlen; i++) {
        if ((p[i] | 0x20) != (type[i] | 0x20)) {
            return 0;
        }
    }
    return 1;
}

static void copy_trim(char *dst, size_t dst_sz, const char *val, size_t val_len)
{
    while (val_len > 0 && (val[0] == ' ' || val[0] == '\t')) { val++; val_len--; }
    while (val_len > 0 && (val[val_len - 1] == ' ' || val[val_len - 1] == '\t')) { val_len--; }
    if (val_len >= dst_sz) {
        val_len = dst_sz - 1;
    }
    if (val_len > 0) {
        os_memcpy(dst, val, val_len);
    }
    dst[val_len] = '\0';
}

/* Build a display name from a structured "N:" value (Family;Given;...). */
static void build_name_from_n(const char *val, size_t val_len)
{
    char tmp[PBAP_NAME_MAX];
    size_t j = 0;

    for (size_t i = 0; i < val_len && j < sizeof(tmp) - 1; i++) {
        char c = val[i];
        tmp[j++] = (c == ';') ? ' ' : c;
    }
    tmp[j] = '\0';
    /* collapse trailing spaces */
    while (j > 0 && tmp[j - 1] == ' ') { tmp[--j] = '\0'; }
    copy_trim(s_cur_name, sizeof(s_cur_name), tmp, os_strlen(tmp));
}

static void parser_commit_card(void)
{
    for (uint8_t i = 0; i < s_cur_tel_cnt; i++) {
        cache_add(s_cur_name, s_cur_tel[i]);
    }
    parser_card_reset();
}

/* Process a single complete vCard line [line, line_end). */
static void parser_handle_line(const char *line, const char *line_end)
{
    while (line < line_end && (*line == ' ' || *line == '\t')) {
        line++;
    }
    if (line >= line_end) {
        return;
    }

    /* Locate ':' (value separator) and the end of the type token. */
    const char *colon = line;
    while (colon < line_end && *colon != ':') {
        colon++;
    }
    if (colon >= line_end) {
        return; /* no value separator */
    }
    const char *type_end = line;
    while (type_end < colon && *type_end != ';') {
        type_end++;
    }
    size_t type_len = (size_t)(type_end - line);

    const char *val     = colon + 1;
    size_t      val_len = (size_t)(line_end - val);

    if (type_is(line, type_len, "BEGIN")) {
        parser_card_reset();
    } else if (type_is(line, type_len, "END")) {
        parser_commit_card();
    } else if (type_is(line, type_len, "FN")) {
        copy_trim(s_cur_name, sizeof(s_cur_name), val, val_len);
    } else if (type_is(line, type_len, "N")) {
        if (s_cur_name[0] == '\0') {
            build_name_from_n(val, val_len);
        }
    } else if (type_is(line, type_len, "TEL")) {
        if (s_cur_tel_cnt < PBAP_CARD_TEL_MAX) {
            copy_trim(s_cur_tel[s_cur_tel_cnt], PBAP_TEL_RAW_MAX, val, val_len);
            if (s_cur_tel[s_cur_tel_cnt][0]) {
                s_cur_tel_cnt++;
            }
        }
    }
}

/* Split [base, base+len) into lines, handling a partial trailing line by saving
 * it to s_tail for the next chunk. */
static void parser_process(const char *base, size_t len)
{
    const char *p   = base;
    const char *end = base + len;

    while (p < end) {
        const char *le = p;
        while (le < end && *le != '\r' && *le != '\n') {
            le++;
        }
        if (le >= end) {
            /* Incomplete line: carry to next chunk. */
            size_t n = (size_t)(end - p);
            if (n > PBAP_TAIL_MAX) {
                n = PBAP_TAIL_MAX;
            }
            os_memcpy(s_tail, p, n);
            s_tail_len = n;
            return;
        }
        if (le > p) {
            parser_handle_line(p, le);
        }
        p = le;
        while (p < end && (*p == '\r' || *p == '\n')) {
            p++;
        }
    }
    s_tail_len = 0;
}

/* Parse one chunk of phonebook body, prepending any carried tail. */
static void parser_feed_chunk(const char *data, size_t len)
{
    if (s_tail_len == 0) {
        parser_process(data, len);
        return;
    }

    size_t ct = s_tail_len;
    size_t cd = len;
    if (ct + cd > PBAP_COMBINED_MAX) {
        cd = PBAP_COMBINED_MAX - ct;
    }
    os_memcpy(s_combined, s_tail, ct);
    if (cd) {
        os_memcpy(s_combined + ct, data, cd);
    }
    s_tail_len = 0;
    parser_process(s_combined, ct + cd);
}

/* Flush a trailing line that had no line terminator on the last chunk. */
static void parser_flush(void)
{
    if (s_tail_len > 0) {
        parser_handle_line(s_tail, s_tail + s_tail_len);
        s_tail_len = 0;
    }
    parser_commit_card(); /* in case a card had no explicit END */
}

/* -------------------------------------------------------------------------- */
/* Worker                                                                      */
/* -------------------------------------------------------------------------- */

static void start_phonebook_pull(const uint8_t *bd_addr)
{
    bk_pbap_pce_get_param_t get_param = {0};
    bt_err_t ret;

    parser_reset();
    rtos_lock_mutex(&s_cache_lock);
    cache_clear_locked();
    rtos_unlock_mutex(&s_cache_lock);

    get_param.get_phonebook_param.object_name    = PBAP_PB_OBJECT_NAME;
    get_param.get_phonebook_param.format         = BK_PBAP_PCE_FORMAT_3_0;
    get_param.get_phonebook_param.filter         = BK_PBAP_PCE_FILTER_FN |
                                                   BK_PBAP_PCE_FILTER_N  |
                                                   BK_PBAP_PCE_FILTER_TEL;
    get_param.get_phonebook_param.max_list_count = PBAP_CONTACTS_MAX;

    ret = bk_bt_pbap_pce_get_phonebook(bd_addr, &get_param);
    if (ret != BK_OK) {
        LOGE("get_phonebook failed %d\n", ret);
    } else {
        LOGI("pulling phonebook %s ...\n", PBAP_PB_OBJECT_NAME);
    }
}

static void worker_handle_connect_req(const uint8_t *bd_addr)
{
    bk_pbap_pce_connect_param_t param = {0};

    rtos_delay_milliseconds(PBAP_CONNECT_DELAY_MS);

    /* Bail out if the HFP link went away (or target changed) while we waited. */
    if (!s_want_connect || os_memcmp(s_target_addr, bd_addr, BK_BD_ADDR_LEN) != 0) {
        return;
    }

    os_memcpy(param.bd_addr, bd_addr, BK_BD_ADDR_LEN);
    param.auth_required = 0;
    param.max_recv_size = BK_PBAP_PCE_DEFAULT_MAX_RECV_SIZE;

    if (bk_bt_pbap_pce_connect(&param) != BK_OK) {
        LOGE("pbap connect request failed\n");
    }
}

static void worker_handle_event(pbap_msg_t *msg)
{
    /* Internal (non-PCE) message kinds are handled outside the enum switch to
     * avoid -Werror=switch on values not in bk_pbap_pce_cb_event_t. */
    if ((int)msg->event == (int)PBAP_MSG_CONNECT_REQ) {
        worker_handle_connect_req(msg->bd_addr);
        return;
    }

    switch (msg->event) {
    case BK_PBAP_PCE_INIT_EVT:
        LOGI("PCE init status=%d\n", msg->status);
        break;

    case BK_PBAP_PCE_CONNECT_CFM_EVT:
        LOGI("PCE connect cfm status=%d\n", msg->status);
        if (msg->status == BK_PBAP_PCE_SUCCESS) {
            start_phonebook_pull(msg->bd_addr);
        } else {
            LOGW("pbap connect not granted (status=%d); caller-ID name disabled\n",
                 msg->status);
        }
        break;

    case BK_PBAP_PCE_GET_PHONEBOOK_CFM_EVT:
        if (msg->data_len > 0 && msg->data_ptr) {
            parser_feed_chunk((const char *)msg->data_ptr, msg->data_len);
        }
        if (msg->status != BK_PBAP_PCE_CONTINUE) {
            parser_flush();
            rtos_lock_mutex(&s_cache_lock);
            os_memcpy(s_cached_addr, msg->bd_addr, BK_BD_ADDR_LEN);
            s_cache_valid = 1;
            LOGI("phonebook cached: %u contacts\n", s_contact_cnt);
            rtos_unlock_mutex(&s_cache_lock);
            /* Session done; we only need the pull, so free the PBAP link. */
            bk_bt_pbap_pce_disconnect(msg->bd_addr);
        }
        break;

    case BK_PBAP_PCE_DISCONNECT_CFM_EVT:
        LOGI("PCE disconnect cfm status=%d\n", msg->status);
        break;

    default:
        break;
    }
}

static void worker_task(beken_thread_arg_t arg)
{
    pbap_msg_t msg;

    (void)arg;
    while (1) {
        if (rtos_pop_from_queue(&s_evt_queue, &msg, BEKEN_WAIT_FOREVER) != kNoErr) {
            break;
        }
        if (msg.event == PBAP_MSG_EXIT) {
            if (msg.data_ptr) {
                os_free(msg.data_ptr);
            }
            break;
        }
        worker_handle_event(&msg);
        if (msg.data_ptr) {
            os_free(msg.data_ptr);
            msg.data_ptr = NULL;
        }
    }
    rtos_delete_thread(NULL);
}

/* -------------------------------------------------------------------------- */
/* PBAP PCE callback (BT stack context): copy + enqueue only                    */
/* -------------------------------------------------------------------------- */

static void copy_body_and_post(pbap_msg_t *msg, const uint8_t *data, uint16_t data_len)
{
    msg->data_ptr = NULL;
    msg->data_len = data_len;
    if (data_len > 0 && data && data_len <= BK_PBAP_PCE_DEFAULT_MAX_RECV_SIZE) {
        msg->data_ptr = (uint8_t *)os_malloc(data_len);
        if (msg->data_ptr) {
            os_memcpy(msg->data_ptr, data, data_len);
        } else {
            msg->data_len = 0;
        }
    }
}

static void pbap_pce_cb(bk_pbap_pce_cb_event_t event, bk_pbap_pce_cb_param_t *param)
{
    pbap_msg_t msg = {0};

    if (!param || !s_evt_queue) {
        return;
    }

    msg.event = event;

    switch (event) {
    case BK_PBAP_PCE_INIT_EVT:
        msg.status = param->init.status;
        break;
    case BK_PBAP_PCE_CONNECT_CFM_EVT:
        msg.status = param->connect_cfm.status;
        os_memcpy(msg.bd_addr, param->connect_cfm.bd_addr, BK_BD_ADDR_LEN);
        break;
    case BK_PBAP_PCE_DISCONNECT_CFM_EVT:
        msg.status = param->disconnect_cfm.status;
        os_memcpy(msg.bd_addr, param->disconnect_cfm.bd_addr, BK_BD_ADDR_LEN);
        break;
    case BK_PBAP_PCE_GET_PHONEBOOK_CFM_EVT:
        msg.status = param->get_phonebook_cfm.status;
        os_memcpy(msg.bd_addr, param->get_phonebook_cfm.bd_addr, BK_BD_ADDR_LEN);
        copy_body_and_post(&msg, param->get_phonebook_cfm.data,
                           param->get_phonebook_cfm.data_len);
        break;
    default:
        /* Not interested in vcard-list/vcard/set/size/abort for name resolution. */
        return;
    }

    if (rtos_push_to_queue(&s_evt_queue, &msg, BEKEN_NO_WAIT) != kNoErr) {
        LOGW("evt queue full, drop event %d\n", (int)event);
        if (msg.data_ptr) {
            os_free(msg.data_ptr);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

static void pbap_gap_event_cb(bk_gap_bt_cb_event_t event, bk_bt_gap_cb_param_t *param);

void pbap_contacts_init(void)
{
    if (s_inited) {
        return;
    }

    if (s_contacts == NULL) {
        s_contacts = (pbap_contact_t *)psram_zalloc(sizeof(pbap_contact_t) * PBAP_CONTACTS_MAX);
        if (s_contacts == NULL) {
            LOGE("psram alloc %u bytes failed\n",
                 (unsigned)(sizeof(pbap_contact_t) * PBAP_CONTACTS_MAX));
            return;
        }
    }

    if (s_cache_lock == NULL) {
        if (rtos_init_mutex(&s_cache_lock) != kNoErr) {
            LOGE("mutex init failed\n");
            return;
        }
    }

    if (s_evt_queue == NULL) {
        if (rtos_init_queue(&s_evt_queue, "pbap_ct_q",
                            sizeof(pbap_msg_t), PBAP_EVT_QUEUE_LEN) != kNoErr) {
            LOGE("queue init failed\n");
            return;
        }
    }

    if (s_evt_thread == NULL) {
        if (rtos_create_thread(&s_evt_thread, PBAP_EVT_TASK_PRIO, "pbap_ct",
                               (beken_thread_function_t)worker_task,
                               PBAP_EVT_TASK_STACK, NULL) != kNoErr) {
            LOGE("worker thread create failed\n");
            rtos_deinit_queue(&s_evt_queue);
            s_evt_queue = NULL;
            return;
        }
    }

    if (bk_bt_pbap_pce_register_callback(pbap_pce_cb) != BK_OK) {
        LOGE("register callback failed\n");
        return;
    }
    if (bk_bt_pbap_pce_init() != BK_OK) {
        LOGE("pce init failed\n");
        return;
    }

    /* Drive PBAP off ACL up/down via bt_manager, independent of HFP/A2DP. */
    if (s_bt_manager_index == 0xFF) {
        btm_callback_s btm_cb = {
            .gap_cb = pbap_gap_event_cb,
        };
        s_bt_manager_index = bt_manager_register_callback(&btm_cb);
    }

    s_inited = 1;
    LOGI("pbap_contacts ready (cap=%u)\n", PBAP_CONTACTS_MAX);
}

/* ACL to a remote device came up: pull its phonebook (unless already cached). */
static void pbap_on_acl_connected(const uint8_t *bd_addr)
{
    pbap_msg_t msg = {0};

    if (!s_inited || !bd_addr || !s_evt_queue) {
        return;
    }

    /* Already have contacts for this exact device: no need to pull again. */
    rtos_lock_mutex(&s_cache_lock);
    uint8_t have = (s_cache_valid && s_contact_cnt > 0 &&
                    os_memcmp(s_cached_addr, bd_addr, BK_BD_ADDR_LEN) == 0);
    rtos_unlock_mutex(&s_cache_lock);
    if (have) {
        LOGI("contacts already cached for this device, skip pull\n");
        return;
    }

    os_memcpy(s_target_addr, bd_addr, BK_BD_ADDR_LEN);
    s_want_connect = 1;

    msg.event = PBAP_MSG_CONNECT_REQ;
    os_memcpy(msg.bd_addr, bd_addr, BK_BD_ADDR_LEN);
    if (rtos_push_to_queue(&s_evt_queue, &msg, BEKEN_NO_WAIT) != kNoErr) {
        LOGW("failed to queue connect req\n");
    }
}

/* ACL to a remote device went down: drop its cached contacts. */
static void pbap_on_acl_disconnected(const uint8_t *bd_addr)
{
    if (!s_inited) {
        return;
    }

    s_want_connect = 0;

    rtos_lock_mutex(&s_cache_lock);
    if (bd_addr == NULL ||
        os_memcmp(s_cached_addr, bd_addr, BK_BD_ADDR_LEN) == 0) {
        cache_clear_locked();
    }
    rtos_unlock_mutex(&s_cache_lock);
}

/* bt_manager GAP callback: PBAP lifecycle is driven purely by ACL up/down, so
 * this module is self-contained and has no dependency on HFP/A2DP. */
static void pbap_gap_event_cb(bk_gap_bt_cb_event_t event, bk_bt_gap_cb_param_t *param)
{
    if (!param) {
        return;
    }

    switch (event) {
    case BK_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        if (param->acl_conn_cmpl_stat.stat == BK_BT_STATUS_SUCCESS) {
            pbap_on_acl_connected(param->acl_conn_cmpl_stat.bda);
        }
        break;

    case BK_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        pbap_on_acl_disconnected(param->acl_disconn_cmpl_stat.bda);
        break;

    default:
        break;
    }
}
