#include "encoder.h"

#include "driver/gpio.h"
#include "esp_timer.h"

/*
 * Two KY-040-style encoders on Lonely Binary N16R8 (see s3-n16r8.jpeg).
 * Display SPI uses the LEFT header (4,8-11,16-18). Encoders use the RIGHT
 * header so wiring stays on the free side of the board.
 *
 * Thinking: CLK41 DT40 SW39
 * Model:    CLK1  DT2  SW42
 */
static const int s_clk[ENCODER_COUNT] = {41, 1};
static const int s_dt[ENCODER_COUNT] = {40, 2};
static const int s_sw[ENCODER_COUNT] = {39, 42};

static int s_last_clk[ENCODER_COUNT];
static int s_last_sw[ENCODER_COUNT];
static int64_t s_last_sw_us[ENCODER_COUNT];
static int s_sw_armed[ENCODER_COUNT];

esp_err_t encoder_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < ENCODER_COUNT; i++) {
        mask |= (1ULL << s_clk[i]) | (1ULL << s_dt[i]) | (1ULL << s_sw[i]);
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < ENCODER_COUNT; i++) {
        s_last_clk[i] = gpio_get_level(s_clk[i]);
        s_last_sw[i] = gpio_get_level(s_sw[i]);
        s_sw_armed[i] = 1;
        s_last_sw_us[i] = 0;
    }
    return ESP_OK;
}

int encoder_delta(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return 0;
    }
    int clk = gpio_get_level(s_clk[id]);
    int dt = gpio_get_level(s_dt[id]);
    int delta = 0;
    if (s_last_clk[id] == 1 && clk == 0) {
        delta = (dt == 1) ? 1 : -1;
    }
    s_last_clk[id] = clk;
    return delta;
}

int encoder_button_pressed(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return 0;
    }
    int sw = gpio_get_level(s_sw[id]);
    int64_t now = esp_timer_get_time();
    int pressed = 0;
    if (s_last_sw[id] == 1 && sw == 0 && s_sw_armed[id]) {
        if (now - s_last_sw_us[id] > 250000) {
            pressed = 1;
            s_last_sw_us[id] = now;
            s_sw_armed[id] = 0;
        }
    }
    if (sw == 1) {
        s_sw_armed[id] = 1;
    }
    s_last_sw[id] = sw;
    return pressed;
}
