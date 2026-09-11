#include "status_led.h"

#if defined(AI_MODEL_PROFILE_SUPERMINI)

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "status_led";
static const uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;

static const rmt_symbol_word_t s_zero = {
    .level0 = 1,
    .duration0 = 3,
    .level1 = 0,
    .duration1 = 9,
};

static const rmt_symbol_word_t s_reset = {
    .level0 = 0,
    .duration0 = 250,
    .level1 = 0,
    .duration1 = 250,
};

static size_t encode_off_frame(const void *data, size_t data_size, size_t symbols_written,
                               size_t symbols_free, rmt_symbol_word_t *symbols, bool *done,
                               void *arg)
{
    (void)data;
    (void)arg;
    if (symbols_written < data_size * 8) {
        if (symbols_free < 8) {
            return 0;
        }
        for (size_t bit = 0; bit < 8; bit++) {
            symbols[bit] = s_zero;
        }
        return 8;
    }
    if (symbols_free < 1) {
        return 0;
    }
    symbols[0] = s_reset;
    *done = true;
    return 1;
}

static void hold_low(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static void ws2812_off(gpio_num_t pin)
{
    rmt_channel_handle_t channel = NULL;
    rmt_encoder_handle_t encoder = NULL;
    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = pin,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&channel_config, &channel);
    if (err == ESP_OK) {
        const rmt_simple_encoder_config_t encoder_config = {.callback = encode_off_frame};
        err = rmt_new_simple_encoder(&encoder_config, &encoder);
    }
    if (err == ESP_OK) {
        err = rmt_enable(channel);
    }
    const uint8_t off[] = {0, 0, 0};
    if (err == ESP_OK) {
        const rmt_transmit_config_t transmit_config = {.loop_count = 0};
        err = rmt_transmit(channel, encoder, off, sizeof(off), &transmit_config);
    }
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(channel, portMAX_DELAY);
    }
    if (encoder) {
        rmt_del_encoder(encoder);
    }
    if (channel) {
        rmt_disable(channel);
        rmt_del_channel(channel);
    }
    hold_low(pin);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d RGB-off failed: %s", (int)pin, esp_err_to_name(err));
    }
}

void status_led_off(void)
{
    /* Super Mini RGB is usually GPIO48; some boards use GPIO21 as LED_BUILTIN. */
    ws2812_off(GPIO_NUM_48);
    hold_low(GPIO_NUM_21);
}

#else

void status_led_off(void) {}

#endif
