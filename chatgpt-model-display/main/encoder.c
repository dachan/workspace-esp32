#include "encoder.h"

#include "driver/gpio.h"
#include "esp_timer.h"

/* Free on Lonely Binary N16R8 with ST7796 display on 4/8-11/16-18. */
#define PIN_ENC_CLK 12
#define PIN_ENC_DT  13
#define PIN_ENC_SW  14

static int s_last_clk;
static int s_last_sw;
static int64_t s_last_sw_us;
static int s_sw_armed;

esp_err_t encoder_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_ENC_CLK) | (1ULL << PIN_ENC_DT) | (1ULL << PIN_ENC_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    s_last_clk = gpio_get_level(PIN_ENC_CLK);
    s_last_sw = gpio_get_level(PIN_ENC_SW);
    s_sw_armed = 1;
    return ESP_OK;
}

int encoder_delta(void)
{
    int clk = gpio_get_level(PIN_ENC_CLK);
    int dt = gpio_get_level(PIN_ENC_DT);
    int delta = 0;
    /* Count on CLK falling edge; DT high => CW (+), low => CCW (-). */
    if (s_last_clk == 1 && clk == 0) {
        delta = (dt == 1) ? 1 : -1;
    }
    s_last_clk = clk;
    return delta;
}

int encoder_button_pressed(void)
{
    int sw = gpio_get_level(PIN_ENC_SW); /* active low */
    int64_t now = esp_timer_get_time();
    int pressed = 0;
    if (s_last_sw == 1 && sw == 0 && s_sw_armed) {
        if (now - s_last_sw_us > 250000) { /* 250 ms debounce */
            pressed = 1;
            s_last_sw_us = now;
            s_sw_armed = 0;
        }
    }
    if (sw == 1) {
        s_sw_armed = 1;
    }
    s_last_sw = sw;
    return pressed;
}
