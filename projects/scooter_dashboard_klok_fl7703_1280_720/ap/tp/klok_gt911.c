/*
 * Project-local GT911 driver for the KLOK FL7703 display board.
 *
 * It deliberately exports the standard GT911 sensor symbols because the
 * project build excludes the SDK's tp_gt911.c and substitutes this source.
 * No SDK GT911 source changes are required.
 */

#include <driver/int.h>
#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include <driver/tp.h>
#include <driver/tp_types.h>
#include <os/mem.h>
#include <os/os.h>
#include "klok_board_profile.h"

#define TAG "klok_gt911"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define GT911_ADDR_PRIMARY          0x14
#define GT911_ADDR_SECONDARY        0x5D
#define GT911_PRODUCT_ID_REG        0x8140
#define GT911_PRODUCT_ID_CODE       0x00313139U
#define GT911_CONFIG_REG            0x8047
#define GT911_STATUS_REG            0x814E
#define GT911_POINT_REG             0x814F

#define GT911_MAX_TOUCHES           5U
#define GT911_POINT_BYTES           8U
#define GT911_CONFIG_BYTES          186U
#define GT911_CONFIG_X_POS          1U
#define GT911_CONFIG_Y_POS          3U
#define GT911_CONFIG_MODULE_POS     6U
#define GT911_POLL_INTERVAL_MS      20U

static uint8_t s_i2c_addr = GT911_ADDR_PRIMARY;
static bool s_touch_down[GT911_MAX_TOUCHES];
static uint16_t s_last_x[GT911_MAX_TOUCHES];
static uint16_t s_last_y[GT911_MAX_TOUCHES];
static uint8_t s_last_width[GT911_MAX_TOUCHES];
static beken_mutex_t s_read_mutex = NULL;
static beken_thread_t s_poll_thread = NULL;
static tp_i2c_callback_t s_poll_cb;

extern bool klok_tp_raw_event_filter(const tp_data_t *tp_data);
static int klok_gt911_clear_status(const tp_i2c_callback_t *cb);
static int klok_gt911_start_polling(const tp_i2c_callback_t *cb);

static int klok_gt911_read(const tp_i2c_callback_t *cb,
                           uint16_t reg,
                           void *data,
                           uint16_t size)
{
    return cb->read_uint16(s_i2c_addr, reg, (uint8_t *)data, size);
}

static int klok_gt911_write(const tp_i2c_callback_t *cb,
                            uint16_t reg,
                            const void *data,
                            uint16_t size)
{
    return cb->write_uint16(s_i2c_addr, reg, (uint8_t *)data, size);
}

static bool klok_gt911_detect(const tp_i2c_callback_t *cb)
{
    static const uint8_t addresses[] = {
        GT911_ADDR_PRIMARY,
        GT911_ADDR_SECONDARY,
    };

    if (cb == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < sizeof(addresses); i++) {
        uint32_t product_id = 0;
        int ret;

        s_i2c_addr = addresses[i];
        ret = klok_gt911_read(cb, GT911_PRODUCT_ID_REG,
                              &product_id, sizeof(product_id));
        LOGI("probe addr=0x%02X ret=%d product=0x%08X\r\n",
             s_i2c_addr, ret, product_id);
        if (ret == BK_OK && product_id == GT911_PRODUCT_ID_CODE) {
            LOGI("detected at 0x%02X\r\n", s_i2c_addr);
            return true;
        }
    }

    return false;
}

static int klok_gt911_reset_and_int_sync(void)
{
    gpio_config_t output = {
        .io_mode = GPIO_OUTPUT_ENABLE,
        .pull_mode = GPIO_PULL_DISABLE,
    };
    gpio_config_t input = {
        .io_mode = GPIO_INPUT_ENABLE,
        .pull_mode = GPIO_PULL_DISABLE,
    };

    /*
     * Goodix reference sequence:
     * RST low >10 ms; select I2C address on INT; RST high; release RST;
     * drive INT low for 50 ms, then switch INT to its interrupt input.
     */
    (void)bk_gpio_disable_interrupt(CONFIG_TP_INT_PIN);

    if (bk_gpio_set_config(CONFIG_TP_RST_PIN, &output) != BK_OK ||
        bk_gpio_set_output_low(CONFIG_TP_RST_PIN) != BK_OK) {
        return BK_FAIL;
    }
    rtos_delay_milliseconds(20);

    if (bk_gpio_set_config(CONFIG_TP_INT_PIN, &output) != BK_OK) {
        return BK_FAIL;
    }
    if (s_i2c_addr == GT911_ADDR_PRIMARY) {
        if (bk_gpio_set_output_high(CONFIG_TP_INT_PIN) != BK_OK) {
            return BK_FAIL;
        }
    } else if (bk_gpio_set_output_low(CONFIG_TP_INT_PIN) != BK_OK) {
        return BK_FAIL;
    }
    rtos_delay_milliseconds(2);

    if (bk_gpio_set_output_high(CONFIG_TP_RST_PIN) != BK_OK) {
        return BK_FAIL;
    }
    rtos_delay_milliseconds(7);
    if (bk_gpio_set_config(CONFIG_TP_RST_PIN, &input) != BK_OK) {
        return BK_FAIL;
    }

    if (bk_gpio_set_output_low(CONFIG_TP_INT_PIN) != BK_OK) {
        return BK_FAIL;
    }
    rtos_delay_milliseconds(50);
    input.pull_mode = GPIO_PULL_UP_EN;
    if (bk_gpio_set_config(CONFIG_TP_INT_PIN, &input) != BK_OK) {
        return BK_FAIL;
    }
    rtos_delay_milliseconds(10);

    LOGI("reset sync complete: addr=0x%02X INT=P%d level=%d\r\n",
         s_i2c_addr, CONFIG_TP_INT_PIN,
         bk_gpio_get_input(CONFIG_TP_INT_PIN));
    return BK_OK;
}

static int klok_gt911_init(const tp_i2c_callback_t *cb,
                           tp_sensor_user_config_t *user)
{
    uint8_t config[GT911_CONFIG_BYTES];

    if (cb == NULL || user == NULL) {
        return BK_FAIL;
    }

    if (klok_gt911_reset_and_int_sync() != BK_OK) {
        LOGE("reset/int sync failed\r\n");
        return BK_FAIL;
    }

    if (klok_gt911_read(cb, GT911_CONFIG_REG,
                        config, sizeof(config)) != BK_OK) {
        LOGE("read factory config failed\r\n");
        return BK_FAIL;
    }

    LOGI("factory config: x=%u y=%u module=0x%02X\r\n",
         (uint16_t)(config[GT911_CONFIG_X_POS] |
                    (config[GT911_CONFIG_X_POS + 1] << 8)),
         (uint16_t)(config[GT911_CONFIG_Y_POS] |
                    (config[GT911_CONFIG_Y_POS + 1] << 8)),
         config[GT911_CONFIG_MODULE_POS]);

    LOGI("using factory config unchanged\r\n");
    if (klok_gt911_clear_status(cb) != BK_OK) {
        LOGE("clear initial status failed\r\n");
        return BK_FAIL;
    }

    os_memset(s_touch_down, 0, sizeof(s_touch_down));
    if (bk_gpio_set_interrupt_type(CONFIG_TP_INT_PIN,
                                   GPIO_INT_TYPE_FALLING_EDGE) != BK_OK) {
        LOGE("set INT falling edge failed\r\n");
        return BK_FAIL;
    }
    if (s_read_mutex == NULL && rtos_init_mutex(&s_read_mutex) != BK_OK) {
        LOGE("init read mutex failed\r\n");
        return BK_FAIL;
    }
    if (bk_gpio_enable_interrupt(CONFIG_TP_INT_PIN) != BK_OK) {
        LOGE("enable INT failed\r\n");
        return BK_FAIL;
    }
    if (klok_gt911_start_polling(cb) != BK_OK) {
        LOGE("start polling failed\r\n");
        return BK_FAIL;
    }
    return BK_OK;
}

static void klok_gt911_map_factory_coordinates(tp_data_t *data)
{
#if KLOK_TP_FACTORY_LANDSCAPE
    const uint16_t raw_x = data->x_coordinate;
    const uint16_t raw_y = data->y_coordinate;

    /*
     * LVGL rotates the 720x1280 display by 270 degrees. Feed it the inverse
     * rotation so the final UI coordinate mirrors GT911's X axis while
     * preserving Y, matching the mounted touch sensor orientation.
     */
    data->x_coordinate = (uint16_t)(KLOK_TP_HOR_SIZE - raw_y - 1U);
    data->y_coordinate = (uint16_t)(KLOK_TP_VER_SIZE - raw_x - 1U);
#else
    (void)data;
#endif
}

static void klok_gt911_emit_up(tp_data_t *data, uint8_t id,
                               const char *source)
{
    data[id].event = TP_EVENT_TYPE_UP;
    data[id].track_id = id;
    data[id].x_coordinate = s_last_x[id];
    data[id].y_coordinate = s_last_y[id];
    data[id].width = s_last_width[id];
    data[id].timestamp = rtos_get_time();
    s_touch_down[id] = false;
    LOGI("TP source=%s UP id=%u x=%u y=%u\r\n",
         source, id, data[id].x_coordinate, data[id].y_coordinate);

    if (klok_tp_raw_event_filter(&data[id])) {
        data[id].event = TP_EVENT_TYPE_NONE;
    } else {
        klok_gt911_map_factory_coordinates(&data[id]);
    }
}

static void klok_gt911_emit_contact(tp_data_t *data,
                                    uint8_t id,
                                    uint16_t x,
                                    uint16_t y,
                                    uint16_t width,
                                    const char *source)
{
    data[id].event = s_touch_down[id] ?
                     TP_EVENT_TYPE_MOVE : TP_EVENT_TYPE_DOWN;
    data[id].track_id = id;
    data[id].x_coordinate = x;
    data[id].y_coordinate = y;
    data[id].width = (uint8_t)width;
    data[id].timestamp = rtos_get_time();
    if (data[id].event == TP_EVENT_TYPE_DOWN) {
        LOGI("TP source=%s DOWN id=%u x=%u y=%u\r\n",
             source, id, x, y);
    }

    s_touch_down[id] = true;
    s_last_x[id] = x;
    s_last_y[id] = y;
    s_last_width[id] = (uint8_t)width;

    if (klok_tp_raw_event_filter(&data[id])) {
        data[id].event = TP_EVENT_TYPE_NONE;
    } else {
        klok_gt911_map_factory_coordinates(&data[id]);
    }
}

static int klok_gt911_clear_status(const tp_i2c_callback_t *cb)
{
    const uint8_t clear = 0;
    return klok_gt911_write(cb, GT911_STATUS_REG, &clear, sizeof(clear));
}

static int klok_gt911_read_tp_info_source(const tp_i2c_callback_t *cb,
                                          uint8_t max_num,
                                          uint8_t *buffer,
                                          const char *source)
{
    uint8_t status = 0;
    uint8_t points[GT911_MAX_TOUCHES * GT911_POINT_BYTES + 1] = {0};
    bool present[GT911_MAX_TOUCHES] = {false};
    tp_data_t *data = (tp_data_t *)buffer;
    int ret = BK_OK;

    if (cb == NULL || data == NULL || max_num == 0 ||
        max_num > GT911_MAX_TOUCHES || s_read_mutex == NULL) {
        return BK_FAIL;
    }

    rtos_lock_mutex(&s_read_mutex);
    if (klok_gt911_read(cb, GT911_STATUS_REG, &status, sizeof(status)) != BK_OK) {
        ret = BK_FAIL;
        goto out;
    }
    if ((status & 0x80U) == 0U) {
        goto out;
    }

    uint8_t count = status & 0x0FU;
    if (count > max_num || count > GT911_MAX_TOUCHES) {
        (void)klok_gt911_clear_status(cb);
        ret = BK_FAIL;
        goto out;
    }

    if (count > 0 &&
        klok_gt911_read(cb, GT911_POINT_REG, points,
                        (uint16_t)(count * GT911_POINT_BYTES + 1U)) != BK_OK) {
        (void)klok_gt911_clear_status(cb);
        ret = BK_FAIL;
        goto out;
    }

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t *point = &points[i * GT911_POINT_BYTES];
        uint8_t id = point[0] & 0x0FU;

        if (id >= max_num || id >= GT911_MAX_TOUCHES) {
            continue;
        }

        present[id] = true;
        klok_gt911_emit_contact(data, id,
                                (uint16_t)(point[1] | (point[2] << 8)),
                                (uint16_t)(point[3] | (point[4] << 8)),
                                (uint16_t)(point[5] | (point[6] << 8)),
                                source);
    }

    for (uint8_t id = 0; id < max_num; id++) {
        if (s_touch_down[id] && !present[id]) {
            klok_gt911_emit_up(data, id, source);
        }
    }

    ret = klok_gt911_clear_status(cb);

out:
    rtos_unlock_mutex(&s_read_mutex);
    return ret;
}

static int klok_gt911_read_tp_info(const tp_i2c_callback_t *cb,
                                   uint8_t max_num,
                                   uint8_t *buffer)
{
    return klok_gt911_read_tp_info_source(cb, max_num, buffer, "IRQ");
}

static void klok_gt911_poll_task(beken_thread_arg_t arg)
{
    (void)arg;

    while (1) {
        tp_data_t data[TP_SUPPORT_MAX_NUM];

        rtos_delay_milliseconds(GT911_POLL_INTERVAL_MS);
        os_memset(data, 0, sizeof(data));
        if (klok_gt911_read_tp_info_source(&s_poll_cb,
                                           TP_SUPPORT_MAX_NUM,
                                           (uint8_t *)data,
                                           "POLL") != BK_OK) {
            continue;
        }

        for (uint8_t i = 0; i < TP_SUPPORT_MAX_NUM; i++) {
            if (data[i].event == TP_EVENT_TYPE_DOWN ||
                data[i].event == TP_EVENT_TYPE_MOVE ||
                data[i].event == TP_EVENT_TYPE_UP) {
                bk_tp_read_info_callback(&data[i]);
            }
        }
    }
}

static int klok_gt911_start_polling(const tp_i2c_callback_t *cb)
{
    if (cb == NULL) {
        return BK_FAIL;
    }
    if (s_poll_thread != NULL) {
        return BK_OK;
    }

    if (s_read_mutex == NULL && rtos_init_mutex(&s_read_mutex) != BK_OK) {
        return BK_FAIL;
    }
    s_poll_cb = *cb;

    return rtos_create_thread(&s_poll_thread, 5, "gt911_poll",
                              klok_gt911_poll_task, 2048, NULL);
}

const tp_sensor_config_t tp_sensor_gt911 = {
    .name = "klok_gt911",
    .def_ppi = PPI_800X480,
    .def_int_type = TP_INT_TYPE_FALLING_EDGE,
    .def_refresh_rate = 10,
    .def_tp_num = GT911_MAX_TOUCHES,
    .id = TP_ID_GT911,
    .address = GT911_ADDR_PRIMARY,
    .detect = klok_gt911_detect,
    .init = klok_gt911_init,
    .read_tp_info = klok_gt911_read_tp_info,
};

const tp_sensor_config_t *gt911_detect_sensor(const tp_i2c_callback_t *cb)
{
    return klok_gt911_detect(cb) ? &tp_sensor_gt911 : NULL;
}

BK_TP_SENSOR_DETECT_SECTION(gt911_detect_sensor);
