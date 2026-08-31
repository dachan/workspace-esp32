#include "board_rgb_led.h"

#include <stdint.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "board_rgb_led";
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

static size_t encode_ws2812_off(const void *data, size_t data_size, size_t symbols_written,
                                size_t symbols_free, rmt_symbol_word_t *symbols, bool *done,
                                void *arg)
{
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

static void send_off_frame(gpio_num_t pin)
{
    rmt_channel_handle_t channel = NULL;
    rmt_encoder_handle_t encoder = NULL;
    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = pin,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    esp_err_t error = rmt_new_tx_channel(&channel_config, &channel);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d RMT channel unavailable: %s", pin, esp_err_to_name(error));
        return;
    }
    rmt_simple_encoder_config_t encoder_config = {
        .callback = encode_ws2812_off,
    };
    error = rmt_new_simple_encoder(&encoder_config, &encoder);
    if (error == ESP_OK) {
        error = rmt_enable(channel);
    }
    const uint8_t off[] = {0, 0, 0};
    if (error == ESP_OK) {
        rmt_transmit_config_t transmit_config = {
            .loop_count = 0,
        };
        error = rmt_transmit(channel, encoder, off, sizeof(off), &transmit_config);
    }
    if (error == ESP_OK) {
        error = rmt_tx_wait_all_done(channel, portMAX_DELAY);
    }
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "sent RGB-off frame on GPIO%d", pin);
    } else {
        ESP_LOGW(TAG, "GPIO%d RGB-off frame failed: %s", pin, esp_err_to_name(error));
    }
    if (encoder) {
        rmt_del_encoder(encoder);
    }
    if (channel) {
        rmt_disable(channel);
        rmt_del_channel(channel);
    }
}

void board_rgb_led_off(void)
{
    // GPIO38 is reserved for the microphone's I2S clock. The v1.1 onboard RGB
    // LED may therefore see that clock, but must not contend with the mic.
    send_off_frame(GPIO_NUM_48);
}
