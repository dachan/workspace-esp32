#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    LED_COUNT = 8,
    LED_DATA_GPIO = GPIO_NUM_1,
    FRAME_INTERVAL_MS = 35,
    MAX_CHANNEL_VALUE = 56,
};

static const char *TAG = "rainbow_wave";
static const uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;

static const rmt_symbol_word_t s_zero = {
    .level0 = 1,
    .duration0 = 3,
    .level1 = 0,
    .duration1 = 9,
};

static const rmt_symbol_word_t s_one = {
    .level0 = 1,
    .duration0 = 9,
    .level1 = 0,
    .duration1 = 3,
};

static const rmt_symbol_word_t s_reset = {
    .level0 = 0,
    .duration0 = 250,
    .level1 = 0,
    .duration1 = 250,
};

static size_t encode_ws2812(const void *data, size_t data_size, size_t symbols_written,
                            size_t symbols_free, rmt_symbol_word_t *symbols, bool *done,
                            void *arg)
{
    (void)arg;
    if (symbols_free < 8) {
        return 0;
    }

    size_t byte_index = symbols_written / 8;
    const uint8_t *bytes = data;
    if (byte_index < data_size) {
        for (size_t bit = 0; bit < 8; ++bit) {
            symbols[bit] = (bytes[byte_index] & (0x80 >> bit)) ? s_one : s_zero;
        }
        return 8;
    }

    symbols[0] = s_reset;
    *done = true;
    return 1;
}

static uint8_t scale_channel(uint8_t value, uint8_t brightness)
{
    return ((uint16_t)value * brightness) / 255;
}

static void color_wheel(uint8_t position, uint8_t brightness,
                        uint8_t *red, uint8_t *green, uint8_t *blue)
{
    if (position < 85) {
        *red = scale_channel(255 - position * 3, brightness);
        *green = scale_channel(position * 3, brightness);
        *blue = 0;
    } else if (position < 170) {
        position -= 85;
        *red = 0;
        *green = scale_channel(255 - position * 3, brightness);
        *blue = scale_channel(position * 3, brightness);
    } else {
        position -= 170;
        *red = scale_channel(position * 3, brightness);
        *green = 0;
        *blue = scale_channel(255 - position * 3, brightness);
    }
}

static uint8_t wave_brightness(uint8_t physical_position, uint8_t phase)
{
    uint8_t distance = physical_position - phase;
    uint8_t folded = distance > 127 ? 255 - distance : distance;
    uint8_t wave = 255 - (folded * 2);
    return 12 + ((uint16_t)wave * (MAX_CHANNEL_VALUE - 12) / 255);
}

static void render_frame(uint8_t frame[LED_COUNT * 3], uint8_t phase)
{
    for (size_t physical_index = 0; physical_index < LED_COUNT; ++physical_index) {
        uint8_t physical_position = physical_index * (256 / LED_COUNT);
        uint8_t brightness = wave_brightness(physical_position, phase);
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        color_wheel(physical_position - phase, brightness, &red, &green, &blue);

        // The strip's DIN is wired at its right end, so its physical left-to-right
        // order is the reverse of the WS2812 logical order.
        size_t logical_index = LED_COUNT - 1 - physical_index;
        frame[logical_index * 3] = green;
        frame[logical_index * 3 + 1] = red;
        frame[logical_index * 3 + 2] = blue;
    }
}

void app_main(void)
{
    rmt_channel_handle_t channel = NULL;
    rmt_encoder_handle_t encoder = NULL;
    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_DATA_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&channel_config, &channel));

    rmt_simple_encoder_config_t encoder_config = {
        .callback = encode_ws2812,
    };
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&encoder_config, &encoder));
    ESP_ERROR_CHECK(rmt_enable(channel));

    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };
    uint8_t frame[LED_COUNT * 3] = {0};
    uint8_t phase = 0;

    ESP_LOGI(TAG, "Starting %d-pixel left-to-right rainbow wave on GPIO%d",
             LED_COUNT, LED_DATA_GPIO);
    while (true) {
        render_frame(frame, phase);
        ESP_ERROR_CHECK(rmt_transmit(channel, encoder, frame, sizeof(frame),
                                     &transmit_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(channel, portMAX_DELAY));
        phase += 3;
        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }
}
