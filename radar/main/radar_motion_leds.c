#include "radar_motion_leds.h"

#include <stdbool.h>

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
    LED_FRAME_INTERVAL_MS = 25,
    MOTION_MAX_MM_PER_SECOND_SQUARED = 10000,
    WAVE_IDLE_BRIGHTNESS = 3,
    WAVE_IDLE_FLOOR_BRIGHTNESS = 1,
    WAVE_MAX_BRIGHTNESS = 64,
    WAVE_SLOW_PHASES_PER_SECOND = 8,
    WAVE_FAST_PHASES_PER_SECOND = 720,
};

static const char *TAG = "motion_leds";
static const uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;

static const rmt_symbol_word_t s_zero = {
    .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9,
};
static const rmt_symbol_word_t s_one = {
    .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3,
};
static const rmt_symbol_word_t s_reset = {
    .level0 = 0, .duration0 = 250, .level1 = 0, .duration1 = 250,
};

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static int16_t s_acceleration_mm_per_second_squared;
static TickType_t s_last_frame_at;

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

static uint8_t motion_brightness(void)
{
    uint32_t magnitude = s_acceleration_mm_per_second_squared < 0
                       ? -(int32_t)s_acceleration_mm_per_second_squared
                       : s_acceleration_mm_per_second_squared;
    uint32_t clamped_magnitude = magnitude > MOTION_MAX_MM_PER_SECOND_SQUARED
                               ? MOTION_MAX_MM_PER_SECOND_SQUARED : magnitude;
    uint32_t peak = WAVE_IDLE_BRIGHTNESS
                  + clamped_magnitude * (WAVE_MAX_BRIGHTNESS - WAVE_IDLE_BRIGHTNESS)
                  / MOTION_MAX_MM_PER_SECOND_SQUARED;
    return (uint8_t)peak;
}

static uint8_t moving_wave_phase(TickType_t now)
{
    uint32_t magnitude = s_acceleration_mm_per_second_squared < 0
                       ? -(int32_t)s_acceleration_mm_per_second_squared
                       : s_acceleration_mm_per_second_squared;
    uint32_t clamped_magnitude = magnitude > MOTION_MAX_MM_PER_SECOND_SQUARED
                               ? MOTION_MAX_MM_PER_SECOND_SQUARED : magnitude;
    uint32_t phases_per_second = WAVE_SLOW_PHASES_PER_SECOND
                               + clamped_magnitude * (WAVE_FAST_PHASES_PER_SECOND
                                                      - WAVE_SLOW_PHASES_PER_SECOND)
                               / MOTION_MAX_MM_PER_SECOND_SQUARED;
    return (uint8_t)(pdTICKS_TO_MS(now) * phases_per_second / 1000);
}

static uint8_t wave_brightness(uint8_t physical_position, uint8_t phase,
                               uint8_t peak_brightness)
{
    uint8_t distance = physical_position - phase;
    uint8_t folded = distance > 127 ? UINT8_MAX - distance : distance;
    uint8_t crest = UINT8_MAX - folded * 2;
    uint8_t floor = peak_brightness / 4;
    if (floor < WAVE_IDLE_FLOOR_BRIGHTNESS) {
        floor = WAVE_IDLE_FLOOR_BRIGHTNESS;
    }
    return floor + (uint16_t)crest * (peak_brightness - floor) / UINT8_MAX;
}

static void render_green_wave(uint8_t frame[LED_COUNT * 3], TickType_t now)
{
    uint8_t peak_brightness = motion_brightness();
    uint8_t phase = moving_wave_phase(now);
    for (int index = 0; index < LED_COUNT; ++index) {
        uint8_t physical_position = index * (UINT8_MAX + 1U) / LED_COUNT;
        uint8_t brightness = wave_brightness(physical_position, phase, peak_brightness);
        // WS2812B pixels are sent in GRB byte order. The changing brightness
        // crest moves along the unchanged green colour.
        frame[index * 3] = brightness;
        frame[index * 3 + 1] = 0;
        frame[index * 3 + 2] = 0;
    }
}

esp_err_t radar_motion_leds_init(void)
{
    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_DATA_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel_config, &s_channel), TAG,
                        "create RMT channel failed");

    rmt_simple_encoder_config_t encoder_config = { .callback = encode_ws2812 };
    esp_err_t error = rmt_new_simple_encoder(&encoder_config, &s_encoder);
    if (error != ESP_OK) {
        rmt_del_channel(s_channel);
        s_channel = NULL;
        return error;
    }

    error = rmt_enable(s_channel);
    if (error != ESP_OK) {
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return error;
    }

    ESP_LOGI(TAG, "motion green wave LEDs ready on GPIO%d", LED_DATA_GPIO);
    return ESP_OK;
}

void radar_motion_leds_update_acceleration(int16_t acceleration_mm_per_second_squared)
{
    s_acceleration_mm_per_second_squared = acceleration_mm_per_second_squared;
}

void radar_motion_leds_step(void)
{
    if (!s_channel || !s_encoder) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (now - s_last_frame_at < pdMS_TO_TICKS(LED_FRAME_INTERVAL_MS)) {
        return;
    }
    s_last_frame_at = now;

    uint8_t frame[LED_COUNT * 3];
    render_green_wave(frame, now);
    const rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    esp_err_t error = rmt_transmit(s_channel, s_encoder, frame, sizeof(frame),
                                   &transmit_config);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "LED frame skipped: %s", esp_err_to_name(error));
        return;
    }
    rmt_tx_wait_all_done(s_channel, pdMS_TO_TICKS(5));
}
