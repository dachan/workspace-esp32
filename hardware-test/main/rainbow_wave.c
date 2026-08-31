#include "rainbow_wave.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "color_palette.h"

enum {
    LED_COUNT = 8,
    LED_DATA_GPIO = GPIO_NUM_1,
    NEXT_PALETTE_BUTTON_GPIO = GPIO_NUM_2,
    PREVIOUS_PALETTE_BUTTON_GPIO = GPIO_NUM_21,
    MAX_CHANNEL_VALUE = 56,
    BASE_PHASE_STEP = 3,
    TOUCH_BOOST_PHASE_STEP = 12,
    TOUCH_BOOST_RAMP_UP_MS = 2500,
    TOUCH_BOOST_RAMP_DOWN_MS = 5000,
    BUTTON_RELEASE_STABLE_MS = 40,
};

static const char *TAG = "rainbow_wave";
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
static uint8_t s_phase;
static uint8_t s_render_phase;
static bool s_ready;
static TickType_t s_touch_boost_started_at;
static uint8_t s_palette_index;
static volatile bool s_next_palette_button_pressed;
static volatile bool s_previous_palette_button_pressed;
static bool s_next_palette_button_armed;
static bool s_previous_palette_button_armed;
static TickType_t s_next_button_released_at;
static TickType_t s_previous_button_released_at;

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

static uint8_t wave_brightness(uint8_t physical_position)
{
    uint8_t distance = physical_position - s_phase;
    uint8_t folded = distance > 127 ? 255 - distance : distance;
    uint8_t wave = 255 - (folded * 2);
    return 12 + ((uint16_t)wave * (MAX_CHANNEL_VALUE - 12) / 255);
}

static void render_frame(uint8_t frame[LED_COUNT * 3])
{
    for (size_t physical_index = 0; physical_index < LED_COUNT; ++physical_index) {
        uint8_t physical_position = physical_index * (256 / LED_COUNT);
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        color_palette_sample(s_palette_index, physical_position - s_phase,
                             wave_brightness(physical_position), &red, &green, &blue);

        // DIN enters the bar at its right end, so logical LED order is reversed.
        size_t logical_index = LED_COUNT - 1 - physical_index;
        frame[logical_index * 3] = green;
        frame[logical_index * 3 + 1] = red;
        frame[logical_index * 3 + 2] = blue;
    }
}

static uint8_t touch_boost_phase_step(void)
{
    if (s_touch_boost_started_at == 0) {
        return BASE_PHASE_STEP;
    }

    uint32_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - s_touch_boost_started_at);
    uint32_t total_duration_ms = TOUCH_BOOST_RAMP_UP_MS + TOUCH_BOOST_RAMP_DOWN_MS;
    if (elapsed_ms >= total_duration_ms) {
        s_touch_boost_started_at = 0;
        return BASE_PHASE_STEP;
    }

    // An asymmetric smoothstep envelope: 2.5 seconds up, then 5 seconds down.
    uint32_t triangle = elapsed_ms <= TOUCH_BOOST_RAMP_UP_MS
                      ? elapsed_ms * 1000 / TOUCH_BOOST_RAMP_UP_MS
                      : (total_duration_ms - elapsed_ms) * 1000 / TOUCH_BOOST_RAMP_DOWN_MS;
    uint32_t smooth = triangle * triangle * (3000 - 2 * triangle) / 1000000;
    return BASE_PHASE_STEP + (TOUCH_BOOST_PHASE_STEP * smooth + 500) / 1000;
}

static void change_palette(int direction)
{
    s_palette_index = (s_palette_index + COLOR_PALETTE_COUNT + direction)
                    % COLOR_PALETTE_COUNT;
    ESP_LOGI(TAG, "Palette %u/%u: %s", s_palette_index + 1, COLOR_PALETTE_COUNT,
             COLOR_PALETTES[s_palette_index].name);
}

void rainbow_wave_next_palette(void)
{
    if (s_ready) {
        change_palette(1);
    }
}

static void next_palette_button_isr(void *arg)
{
    (void)arg;
    s_next_palette_button_pressed = true;
}

static void previous_palette_button_isr(void *arg)
{
    (void)arg;
    s_previous_palette_button_pressed = true;
}

static void poll_palette_button(gpio_num_t gpio, volatile bool *pressed, bool *armed,
                                TickType_t *released_at, int direction)
{
    TickType_t now = xTaskGetTickCount();
    bool released = gpio_get_level(gpio) != 0;

    if (!released) {
        *released_at = 0;
    } else if (!*armed) {
        if (*released_at == 0) {
            *released_at = now;
        } else if (now - *released_at >= pdMS_TO_TICKS(BUTTON_RELEASE_STABLE_MS)) {
            *armed = true;
        }
    }

    if (!*pressed) {
        return;
    }
    *pressed = false;
    if (!*armed) {
        return;
    }

    change_palette(direction);
    *armed = false;
    *released_at = 0;
}

esp_err_t rainbow_wave_init(void)
{
    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_DATA_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    esp_err_t result = rmt_new_tx_channel(&channel_config, &s_channel);
    if (result != ESP_OK) {
        return result;
    }
    rmt_simple_encoder_config_t encoder_config = { .callback = encode_ws2812 };
    result = rmt_new_simple_encoder(&encoder_config, &s_encoder);
    if (result != ESP_OK) {
        rmt_del_channel(s_channel);
        s_channel = NULL;
        return result;
    }
    result = rmt_enable(s_channel);
    if (result != ESP_OK) {
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return result;
    }
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << NEXT_PALETTE_BUTTON_GPIO)
                      | (1ULL << PREVIOUS_PALETTE_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    result = gpio_config(&button_config);
    if (result != ESP_OK) {
        rmt_disable(s_channel);
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return result;
    }
    result = gpio_install_isr_service(0);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        rmt_disable(s_channel);
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return result;
    }
    result = gpio_isr_handler_add(NEXT_PALETTE_BUTTON_GPIO, next_palette_button_isr, NULL);
    if (result != ESP_OK) {
        rmt_disable(s_channel);
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return result;
    }
    result = gpio_isr_handler_add(PREVIOUS_PALETTE_BUTTON_GPIO,
                                  previous_palette_button_isr, NULL);
    if (result != ESP_OK) {
        gpio_isr_handler_remove(NEXT_PALETTE_BUTTON_GPIO);
        rmt_disable(s_channel);
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return result;
    }
    int next_button_idle = gpio_get_level(NEXT_PALETTE_BUTTON_GPIO);
    int previous_button_idle = gpio_get_level(PREVIOUS_PALETTE_BUTTON_GPIO);
    s_next_palette_button_armed = next_button_idle != 0;
    s_previous_palette_button_armed = previous_button_idle != 0;
    TickType_t now = xTaskGetTickCount();
    s_next_button_released_at = s_next_palette_button_armed ? now : 0;
    s_previous_button_released_at = s_previous_palette_button_armed ? now : 0;
    s_ready = true;
    ESP_LOGI(TAG, "External 8-pixel rainbow bar GPIO%d; next button GPIO%d idle=%d; previous button GPIO%d idle=%d",
             LED_DATA_GPIO, NEXT_PALETTE_BUTTON_GPIO, next_button_idle,
             PREVIOUS_PALETTE_BUTTON_GPIO, previous_button_idle);
    return ESP_OK;
}

void rainbow_wave_trigger(void)
{
    if (s_ready) {
        s_touch_boost_started_at = xTaskGetTickCount();
    }
}

void rainbow_wave_off(void)
{
    if (!s_ready) {
        return;
    }
    uint8_t frame[LED_COUNT * 3] = {0};
    const rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    if (rmt_transmit(s_channel, s_encoder, frame, sizeof(frame), &transmit_config) == ESP_OK) {
        rmt_tx_wait_all_done(s_channel, pdMS_TO_TICKS(20));
    }
}

uint8_t rainbow_wave_palette_index(void)
{
    return s_palette_index;
}

uint8_t rainbow_wave_color_phase(void)
{
    return s_render_phase;
}

void rainbow_wave_step(void)
{
    if (!s_ready) {
        return;
    }
    poll_palette_button(NEXT_PALETTE_BUTTON_GPIO, &s_next_palette_button_pressed,
                        &s_next_palette_button_armed, &s_next_button_released_at, 1);
    poll_palette_button(PREVIOUS_PALETTE_BUTTON_GPIO, &s_previous_palette_button_pressed,
                        &s_previous_palette_button_armed, &s_previous_button_released_at, -1);
    uint8_t frame[LED_COUNT * 3];
    s_render_phase = s_phase;
    render_frame(frame);
    const rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    esp_err_t result = rmt_transmit(s_channel, s_encoder, frame, sizeof(frame), &transmit_config);
    if (result == ESP_OK) {
        result = rmt_tx_wait_all_done(s_channel, 10);
    }
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Rainbow frame skipped: %s", esp_err_to_name(result));
        return;
    }
    s_phase += touch_boost_phase_step();
}
