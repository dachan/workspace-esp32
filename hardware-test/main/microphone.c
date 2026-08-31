#include "microphone.h"

#include <inttypes.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    MIC_BCLK_GPIO = 38,
    MIC_WS_GPIO = 39,
    MIC_DATA_GPIO = 40,
    MIC_SAMPLE_RATE = 16000,
    MIC_MAX_SAMPLE_COUNT = 1024,
    MIC_MIN_TRIGGER_LEVEL = 800,
    MIC_COOLDOWN_MS = 400,
};

static const char *TAG = "microphone";
static i2s_chan_handle_t s_rx;
static int32_t s_samples[MIC_MAX_SAMPLE_COUNT];
static uint32_t s_noise_floor;
static bool s_noise_floor_ready;
static TickType_t s_last_trigger;

static uint32_t sound_level(const int16_t *samples, size_t count)
{
    uint64_t total = 0;
    for (size_t sample = 0; sample < count; ++sample) {
        // Keep the same level scale as the original 24-bit I2S samples.
        int32_t value = (int32_t)samples[sample] << 2;
        uint32_t magnitude = value < 0 ? -(int64_t)value : value;
        total += magnitude;
    }
    return count ? total / count : 0;
}

esp_err_t microphone_init(void)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,
                                                                    I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 4;
    channel_config.dma_frame_num = MIC_MAX_SAMPLE_COUNT;
    esp_err_t error = i2s_new_channel(&channel_config, NULL, &s_rx);
    if (error != ESP_OK) {
        return error;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK_GPIO,
            .ws = MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_DATA_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    // INMP441's L/R pin is wired low, so it drives the left I2S slot.
    standard_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    error = i2s_channel_init_std_mode(s_rx, &standard_config);
    if (error == ESP_OK) {
        error = i2s_channel_enable(s_rx);
    }
    if (error != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return error;
    }

    ESP_LOGI(TAG, "I2S mic ready: BCLK=%d WS=%d DATA=%d at %d Hz",
             MIC_BCLK_GPIO, MIC_WS_GPIO, MIC_DATA_GPIO, MIC_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t microphone_read_pcm16(int16_t *samples, size_t sample_count,
                                size_t *samples_read, TickType_t timeout)
{
    if (!s_rx || !samples || !samples_read || sample_count > MIC_MAX_SAMPLE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_read = 0;
    esp_err_t error = i2s_channel_read(s_rx, s_samples,
                                       sample_count * sizeof(s_samples[0]),
                                       &bytes_read, timeout);
    if (error != ESP_OK) {
        return error;
    }

    *samples_read = bytes_read / sizeof(s_samples[0]);
    for (size_t sample = 0; sample < *samples_read; ++sample) {
        // INMP441's 24-bit sample is left-aligned in its 32-bit I2S slot.
        samples[sample] = s_samples[sample] >> 16;
    }
    return ESP_OK;
}

bool microphone_sound_detected_pcm(const int16_t *samples, size_t sample_count)
{
    uint32_t level = sound_level(samples, sample_count);
    if (!s_noise_floor_ready) {
        s_noise_floor = level;
        s_noise_floor_ready = true;
        return false;
    }

    uint32_t threshold = s_noise_floor * 5;
    if (threshold < MIC_MIN_TRIGGER_LEVEL) {
        threshold = MIC_MIN_TRIGGER_LEVEL;
    }
    TickType_t now = xTaskGetTickCount();
    bool detected = level > threshold
                 && now - s_last_trigger >= pdMS_TO_TICKS(MIC_COOLDOWN_MS);
    if (detected) {
        s_last_trigger = now;
        ESP_LOGI(TAG, "sound detected: level=%" PRIu32 " threshold=%" PRIu32,
                 level, threshold);
    }

    // Follow room noise slowly, but never let a clap immediately raise the
    // threshold high enough to hide the next sound.
    if (level <= threshold) {
        s_noise_floor = (s_noise_floor * 31 + level) / 32;
    }
    return detected;
}

bool microphone_sound_detected(void)
{
    int16_t samples[MIC_MAX_SAMPLE_COUNT];
    size_t samples_read = 0;
    if (microphone_read_pcm16(samples, MIC_MAX_SAMPLE_COUNT, &samples_read,
                              pdMS_TO_TICKS(10)) != ESP_OK) {
        return false;
    }
    return microphone_sound_detected_pcm(samples, samples_read);
}
