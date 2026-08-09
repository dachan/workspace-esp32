#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// In the MAX98357 breakout's own silkscreen order (LRC, BCLK, DIN, GAIN, SD,
// GND, VIN) rather than grouped by I2S signal — GAIN and VIN are analog/power
// and aren't wired to a GPIO, so only the four digital pins appear here.
// Kept on GPIO38-42 for the future full-duplex bus: the microphone can later
// share this same BCLK/WS and take GPIO41 as its data output.
#define PIN_AMP_WS   39   // LRC  (1st on the silkscreen)
#define PIN_AMP_BCLK 38   // BCLK (2nd)
#define PIN_AMP_DIN  40   // DIN  (3rd)
                          // GAIN (4th) — not wired, floats to 9dB
#define PIN_AMP_SD   42   // SD   (5th)
                          // GND, VIN (6th/7th) — power, not GPIO

#define SAMPLE_RATE 22050
#define CHUNK_FRAMES 128
#define PI 3.14159265358979323846f

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx;
static TaskHandle_t s_audio_task;

// DMA-sized audio scratch belongs in static storage, never on a task stack.
// Each frame is duplicated into left and right so the amp sounds regardless of
// which MAX98357 channel mode its SD pin selects.
static int16_t s_samples[CHUNK_FRAMES * 2];

static void write_touch_sound(bool on_creature)
{
    const int sound_ms = on_creature ? 140 : 250;
    const int total_frames = SAMPLE_RATE * sound_ms / 1000;
    const float start_frequency = on_creature ? 610.0f : 650.0f;
    const float end_frequency = on_creature ? 1050.0f : 470.0f;
    const float amplitude = on_creature ? 9600.0f : 7600.0f;
    const int attack_frames = on_creature ? 180 : 110;
    const int release_frames = on_creature ? 420 : 2205;
    float phase = 0.0f;

    // SD is the final safety boundary: the amplifier is physically shut down
    // while idle, regardless of what the I2S peripheral does on underrun.
    gpio_set_level(PIN_AMP_SD, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    for (int first = 0; first < total_frames; first += CHUNK_FRAMES) {
        int count = total_frames - first;
        if (count > CHUNK_FRAMES) {
            count = CHUNK_FRAMES;
        }

        for (int i = 0; i < count; i++) {
            int frame = first + i;
            float progress = (float)frame / (float)total_frames;
            float frequency = start_frequency + (end_frequency - start_frequency) * progress;
            float envelope = 1.0f;

            if (frame < attack_frames) {
                envelope = (float)frame / (float)attack_frames;
            }
            int remaining = total_frames - frame;
            if (remaining < release_frames) {
                envelope *= (float)remaining / (float)release_frames;
            }

            // Still below full scale for headroom on the 1W speaker and the
            // breakout's default gain, but loud enough for an enclosed pet.
            int16_t sample = (int16_t)(sinf(phase) * envelope * amplitude);
            s_samples[i * 2] = sample;
            s_samples[i * 2 + 1] = sample;

            phase += 2.0f * PI * frequency / (float)SAMPLE_RATE;
            if (phase >= 2.0f * PI) {
                phase -= 2.0f * PI;
            }
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(s_tx, s_samples,
                                          count * 2 * sizeof(s_samples[0]),
                                          &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "touch sound write failed: %s", esp_err_to_name(err));
            gpio_set_level(PIN_AMP_SD, 0);
            return;
        }
    }

    memset(s_samples, 0, sizeof(s_samples));
    size_t bytes_written = 0;
    i2s_channel_write(s_tx, s_samples, sizeof(s_samples),
                      &bytes_written, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(40));
    gpio_set_level(PIN_AMP_SD, 0);
}

static void audio_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t sound = 0;
        xTaskNotifyWait(0, UINT32_MAX, &sound, portMAX_DELAY);
        write_touch_sound(sound == 1u);
    }
}

esp_err_t audio_init(void)
{
    gpio_config_t sd_cfg = {
        .pin_bit_mask = 1ULL << PIN_AMP_SD,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&sd_cfg), TAG, "amp SD GPIO");
    gpio_set_level(PIN_AMP_SD, 0);

    i2s_chan_config_t channel_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_cfg.dma_desc_num = 6;
    channel_cfg.dma_frame_num = CHUNK_FRAMES;
    // The default is false, which repeats stale DMA contents on underrun and
    // can turn a one-shot chirp into continuous output.
    channel_cfg.auto_clear_after_cb = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_cfg, &s_tx, NULL), TAG, "I2S channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_AMP_BCLK,
            .ws = PIN_AMP_WS,
            .dout = PIN_AMP_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "I2S mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "I2S enable");

    BaseType_t created = xTaskCreate(audio_task, "audio", 3072, NULL, 5, &s_audio_task);
    if (created != pdPASS) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "MAX98357 ready: LRC=%d BCLK=%d DIN=%d SD=%d, %d Hz stereo; idle muted",
             PIN_AMP_WS, PIN_AMP_BCLK, PIN_AMP_DIN, PIN_AMP_SD, SAMPLE_RATE);
    return ESP_OK;
}

void audio_play_touch(bool on_creature)
{
    if (s_audio_task != NULL) {
        ESP_LOGI(TAG, "%s touch sound", on_creature ? "creature" : "background");
        xTaskNotify(s_audio_task, on_creature ? 1u : 2u, eSetValueWithOverwrite);
    }
}
