#include "touch.h"

#include <string.h>

#include "calibrate.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    TOUCH_SDA = 6,
    TOUCH_SCL = 15,
    TOUCH_RST = 7,
    TOUCH_INT = 5,
    TOUCH_ADDR = 0x38,
    TOUCH_REG_POINTS = 0x02,
    NATIVE_W = 320,
    NATIVE_H = 480,
    TOUCH_RELEASE_DEBOUNCE_US = 150 * 1000,
};

static const char *TAG = "touch";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_ready;
static bool s_down;
static int64_t s_down_us;
static int64_t s_last_touch_us;
static int s_last_x;
static int s_last_y;

bool touch_raw(uint16_t *raw_x, uint16_t *raw_y)
{
    if (!s_ready || !raw_x || !raw_y) {
        return false;
    }
    uint8_t reg = TOUCH_REG_POINTS;
    uint8_t data[5] = {0};
    if (i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), 50) != ESP_OK
        || (data[0] & 0x0f) == 0) {
        return false;
    }
    uint16_t x = ((uint16_t)(data[1] & 0x0f) << 8) | data[2];
    uint16_t y = ((uint16_t)(data[3] & 0x0f) << 8) | data[4];
    if (x >= NATIVE_W || y >= NATIVE_H) {
        x = (uint16_t)((uint32_t)x * NATIVE_W / 4096);
        y = (uint16_t)((uint32_t)y * NATIVE_H / 4096);
    }
    *raw_x = x;
    *raw_y = y;
    return true;
}

esp_err_t touch_init(void)
{
    const gpio_config_t reset = {
        .pin_bit_mask = 1ULL << TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&reset);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(250));

    const gpio_config_t interrupt = {
        .pin_bit_mask = 1ULL << TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&interrupt);
    if (err != ESP_OK) {
        return err;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_probe(s_bus, TOUCH_ADDR, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT6336 not found on SDA=%d SCL=%d", TOUCH_SDA, TOUCH_SCL);
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "FT6336 ready on SDA=%d SCL=%d RST=%d INT=%d",
             TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
    return ESP_OK;
}

void touch_clear_state(void)
{
    s_down = false;
    s_down_us = 0;
    s_last_touch_us = 0;
}

bool touch_poll(touch_sample_t *ev)
{
    if (!ev) {
        return false;
    }
    memset(ev, 0, sizeof(*ev));
    if (!s_ready) {
        return false;
    }
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    bool sampled_down = touch_raw(&raw_x, &raw_y);
    int64_t now = esp_timer_get_time();
    if (sampled_down) {
        s_last_touch_us = now;
    }
    // A failed FT6336 read looks like no contact. Keep the prior contact live
    // long enough to prevent a transient I2C miss from becoming a false release.
    bool down = sampled_down || (s_down && now - s_last_touch_us < TOUCH_RELEASE_DEBOUNCE_US);
    ev->pressed = down && !s_down;
    ev->released = !down && s_down;
    if (down) {
        if (!s_down) {
            s_down_us = now;
        }
        ev->held_ms = (int)((now - s_down_us) / 1000);
        if (sampled_down) {
            calibrate_map(raw_x, raw_y, &ev->x, &ev->y);
            s_last_x = ev->x;
            s_last_y = ev->y;
        } else {
            ev->x = s_last_x;
            ev->y = s_last_y;
        }
    } else {
        ev->x = s_last_x;
        ev->y = s_last_y;
        if (s_down) {
            ev->held_ms = (int)((s_last_touch_us - s_down_us) / 1000);
        }
    }
    ev->down = down;
    s_down = down;
    return true;
}
