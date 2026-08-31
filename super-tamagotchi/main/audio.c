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
// These four are the configuration that was measured working on hardware. A
// spell on GPIO21/47/48/2 produced no sound and lit the onboard RGB LED, which
// sits on GPIO48 — do not move the amp there again.
// The microphone can later share this BCLK/WS and take GPIO41 as its data line.
#define PIN_AMP_WS   39   // LRC  (1st on the silkscreen)
#define PIN_AMP_BCLK 38   // BCLK (2nd)
#define PIN_AMP_DIN  40   // DIN  (3rd)
                          // GAIN (4th) — not wired, floats to 9dB
#define PIN_AMP_SD   42   // SD   (5th)
                          // GND, VIN (6th/7th) — power, not GPIO

// The MAX98357 auto-detects its clocking and only locks onto LRCLK at 8, 16,
// 32, 44.1, 48, 88.2 or 96kHz. 11.025, 12, 22.05 and 24kHz are explicitly
// unsupported (datasheet, "LRCLK Polarity") — the amp simply stays silent, with
// no error anywhere in the I2S driver. Do not pick a rate outside that list.
#define SAMPLE_RATE 44100
#define CHUNK_FRAMES 128
#define DMA_DESC_COUNT 6
#define PI 3.14159265358979323846f
#define FRAMES_MS(ms) (SAMPLE_RATE * (ms) / 1000)

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx;
static TaskHandle_t s_audio_task;

// DMA-sized audio scratch belongs in static storage, never on a task stack.
// Each frame is duplicated into left and right so the amp sounds regardless of
// which MAX98357 channel mode its SD pin selects.
static int16_t s_samples[CHUNK_FRAMES * 2];

// Every sound goes through here. The fade in and out are not decoration: a tone
// that starts or stops at a non-zero sample is a step discontinuity, which is
// precisely what a click is. Phase carries across calls so consecutive tones
// join without a discontinuity at the seam either.
static void write_tone(float start_hz, float end_hz, float amplitude,
                       int ms, int attack_ms, int release_ms, float *phase)
{
    const int total_frames = SAMPLE_RATE * ms / 1000;
    const int attack_frames = FRAMES_MS(attack_ms);
    const int release_frames = FRAMES_MS(release_ms);

    for (int first = 0; first < total_frames; first += CHUNK_FRAMES) {
        int count = total_frames - first;
        if (count > CHUNK_FRAMES) {
            count = CHUNK_FRAMES;
        }

        for (int i = 0; i < count; i++) {
            int frame = first + i;
            float progress = (float)frame / (float)total_frames;
            float frequency = start_hz + (end_hz - start_hz) * progress;
            float envelope = 1.0f;

            if (frame < attack_frames) {
                envelope = (float)frame / (float)attack_frames;
            }
            int remaining = total_frames - frame;
            if (remaining < release_frames) {
                envelope *= (float)remaining / (float)release_frames;
            }

            int16_t sample = (int16_t)(sinf(*phase) * envelope * amplitude);
            s_samples[i * 2] = sample;
            s_samples[i * 2 + 1] = sample;

            *phase += 2.0f * PI * frequency / (float)SAMPLE_RATE;
            if (*phase >= 2.0f * PI) {
                *phase -= 2.0f * PI;
            }
        }

        size_t bytes_written = 0;
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_write(s_tx, s_samples,
                                                        count * 2 * sizeof(s_samples[0]),
                                                        &bytes_written, portMAX_DELAY));
    }
}

// Leaves the DMA ring holding silence so the permanently-enabled amp idles on
// zero rather than on the tail of whatever played last.
static void write_silence(void)
{
    memset(s_samples, 0, sizeof(s_samples));
    for (int i = 0; i < DMA_DESC_COUNT; i++) {
        size_t bytes_written = 0;
        i2s_channel_write(s_tx, s_samples, sizeof(s_samples),
                          &bytes_written, portMAX_DELAY);
    }
}

// Rendered on its own task: a touch sound is 140-250ms and the render loop must
// not block on it, or the creature visibly stalls on every poke.
static void audio_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t sound = 0;
        xTaskNotifyWait(0, UINT32_MAX, &sound, portMAX_DELAY);

        bool on_creature = (sound == 1u);
        float phase = 0.0f;
        write_tone(on_creature ? 620.0f : 660.0f,
                   on_creature ? 940.0f : 500.0f,
                   on_creature ? 9000.0f : 7500.0f,
                   on_creature ? 150 : 220,
                   12, on_creature ? 60 : 110,
                   &phase);
        write_silence();
    }
}

void audio_play_touch(bool on_creature)
{
    if (s_audio_task != NULL) {
        xTaskNotify(s_audio_task, on_creature ? 1u : 2u, eSetValueWithOverwrite);
    }
}

void audio_play_startup(void)
{
    // Three short ascending notes — G5, B5, E6. Short and pitched is what reads
    // as a device waking up; long flat tones read as a test signal or an alarm.
    static const float notes[] = { 784.0f, 988.0f, 1319.0f };
    float phase = 0.0f;

    for (int i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        write_tone(notes[i], notes[i], 9000.0f, 110, 20, 55, &phase);
        write_silence();
        vTaskDelay(pdMS_TO_TICKS(45));
    }
    ESP_LOGI(TAG, "startup chime played");
}

// Loud continuous tone with SD held high throughout, run before the render loop
// starts. Strips out every software variable at once: no gating on touch, no
// SD toggling, no short envelope to miss, and no SPI or backlight load on the
// rail. If this is silent, the fault is physical — power, wiring, or the amp.
void audio_selftest(void)
{
    // Three levels, quietest first. Current draw rises with amplitude, so a
    // resistive supply contact fails the loud step while the quiet one survives
    // — which distinguishes bad power delivery from a bad signal connection.
    // Each step fades in and out over 60ms so the steps themselves are not
    // audible as clicks; anything crackly that remains is the hardware.
    static const float levels[] = { 1500.0f, 5500.0f, 13000.0f };
    const int step_ms = 1200;
    float phase = 0.0f;

    gpio_set_level(PIN_AMP_SD, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    for (int level = 0; level < sizeof(levels) / sizeof(levels[0]); level++) {
        ESP_LOGW(TAG, "SELFTEST: 1kHz at amplitude %.0f", levels[level]);
        write_tone(1000.0f, 1000.0f, levels[level], step_ms, 60, 60, &phase);
        write_silence();
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    ESP_LOGW(TAG, "SELFTEST: done — amp stays enabled from here");
}

// Call before anything else at boot. GPIO39-42 are the JTAG pins and come out
// of reset with internal pull-ups, so SD floats high enough to switch the amp
// on by itself — while BCLK, LRC and DIN are still floating inputs picking up
// whatever the display's SPI is doing. The result is amplified garbage during
// bring-up, before audio_init has run at all.
esp_err_t audio_early_mute(void)
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
    ESP_LOGI(TAG, "amp held in shutdown until audio_init");
    return ESP_OK;
}

esp_err_t audio_init(void)
{
    ESP_RETURN_ON_ERROR(audio_early_mute(), TAG, "amp SD GPIO");

    i2s_chan_config_t channel_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_cfg.dma_desc_num = DMA_DESC_COUNT;
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

    // Only now are BCLK, LRC and DIN actually driven, so this is the first
    // moment it is safe to let the amp out of shutdown.
    write_silence();
    gpio_set_level(PIN_AMP_SD, 1);

    if (xTaskCreate(audio_task, "audio", 3072, NULL, 5, &s_audio_task) != pdPASS) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "MAX98357 ready: LRC=%d BCLK=%d DIN=%d SD=%d, %d Hz stereo; amp enabled",
             PIN_AMP_WS, PIN_AMP_BCLK, PIN_AMP_DIN, PIN_AMP_SD, SAMPLE_RATE);
    return ESP_OK;
}
