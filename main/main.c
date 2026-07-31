// Step 0 bring-up: prove the board and toolchain before a single peripheral wire.
//
// Validates, in order:
//   1. Toolchain, flash, and serial console  — we got here at all
//   2. Chip identity                          — really an ESP32-S3, revision, cores
//   3. Flash size                             — expect 16MB on N16R8
//   4. PSRAM enumerates                       — expect ~8MB
//   5. PSRAM actually stores data             — write/verify, not just a size report
//
// Step 5 is the one worth having. Misconfigured octal PSRAM often enumerates
// happily and then returns garbage, which surfaces much later as inexplicable
// framebuffer corruption that looks like a display wiring fault.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "touch.h"

static const char *TAG = "step0";

// Large enough to span multiple PSRAM pages, small enough to always fit in 8MB.
#define PSRAM_TEST_BYTES (4 * 1024 * 1024)

static void report_chip(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "chip      : %s, rev %d.%d, %d core%s",
             CONFIG_IDF_TARGET,
             chip.revision / 100, chip.revision % 100,
             chip.cores, chip.cores == 1 ? "" : "s");
    ESP_LOGI(TAG, "radio     : %s%s",
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi b/g/n " : "",
             (chip.features & CHIP_FEATURE_BLE) ? "BLE" : "");

    uint32_t flash_bytes = 0;
    if (esp_flash_get_size(NULL, &flash_bytes) == ESP_OK) {
        ESP_LOGI(TAG, "flash     : %" PRIu32 " MB (expect 16)", flash_bytes / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "flash     : size read FAILED");
    }
}

// Write a position-dependent pattern across the whole buffer, then read it back.
// A position-dependent value catches address aliasing, which a constant fill misses.
static bool psram_write_verify(uint32_t *buf, size_t words)
{
    for (size_t i = 0; i < words; i++) {
        buf[i] = (uint32_t)i * 2654435761u;  // Knuth multiplicative hash
    }

    for (size_t i = 0; i < words; i++) {
        uint32_t expect = (uint32_t)i * 2654435761u;
        if (buf[i] != expect) {
            ESP_LOGE(TAG, "psram     : MISMATCH at word %zu — wrote 0x%08" PRIx32
                          ", read 0x%08" PRIx32, i, expect, buf[i]);
            return false;
        }
    }
    return true;
}

static bool report_psram(void)
{
    size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (total == 0) {
        ESP_LOGE(TAG, "psram     : NOT DETECTED");
        ESP_LOGE(TAG, "            N16R8 is octal PSRAM. Check CONFIG_SPIRAM=y and");
        ESP_LOGE(TAG, "            CONFIG_SPIRAM_MODE_OCT=y in sdkconfig.defaults,");
        ESP_LOGE(TAG, "            then `idf.py fullclean` and rebuild.");
        return false;
    }

    ESP_LOGI(TAG, "psram     : %zu KB detected (expect ~8192)", total / 1024);
    ESP_LOGI(TAG, "psram free: %zu KB, largest block %zu KB",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024);

    uint32_t *buf = heap_caps_malloc(PSRAM_TEST_BYTES, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(TAG, "psram     : could not allocate %d MB for the write test",
                 PSRAM_TEST_BYTES / (1024 * 1024));
        return false;
    }

    bool ok = psram_write_verify(buf, PSRAM_TEST_BYTES / sizeof(uint32_t));
    heap_caps_free(buf);

    ESP_LOGI(TAG, "psram     : %d MB write/verify %s",
             PSRAM_TEST_BYTES / (1024 * 1024), ok ? "PASSED" : "FAILED");
    return ok;
}

// Step 1: prove the panel. The sequence is diagnostic, not decorative — each
// stage isolates a different failure, so what you see on the glass says which
// thing is wrong rather than just "it didn't work".
static void step1_display(void)
{
    ESP_LOGI(TAG, "==== step 1: display ====");

    // Backlight before anything else. It is the only part of the panel that is
    // independent of SPI, so it separates "no power / no pin" from "no data".
    display_backlight_selftest(3);

    esp_err_t err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "  check wiring: SCK=12 MOSI=11 CS=10 DC=9 RST=14 LED=21, VCC=3V3");
        return;
    }
    display_set_backlight(100);

    // Stage 1 — full-screen primaries. If these come up in the wrong colours the
    // fault is the BGR/RGB element order, not the wiring.
    const struct { const char *name; uint16_t colour; } primaries[] = {
        {"red",   display_rgb(255, 0, 0)},
        {"green", display_rgb(0, 255, 0)},
        {"blue",  display_rgb(0, 0, 255)},
    };
    for (size_t i = 0; i < sizeof(primaries) / sizeof(primaries[0]); i++) {
        ESP_LOGI(TAG, "  filling %s", primaries[i].name);
        display_fill(primaries[i].colour);
        ESP_ERROR_CHECK_WITHOUT_ABORT(display_flush());
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    // Stage 2 — an orientation chart that stays up. Each corner is a different
    // colour, so which corner is which tells us whether swap_xy and mirror are
    // right without guessing.
    display_fill(display_rgb(16, 16, 24));
    const int cw = 60, ch = 45;
    display_fill_rect(0, 0, cw, ch, display_rgb(255, 0, 0));                                   // TL red
    display_fill_rect(DISPLAY_WIDTH - cw, 0, cw, ch, display_rgb(0, 255, 0));                  // TR green
    display_fill_rect(0, DISPLAY_HEIGHT - ch, cw, ch, display_rgb(0, 80, 255));                // BL blue
    display_fill_rect(DISPLAY_WIDTH - cw, DISPLAY_HEIGHT - ch, cw, ch, display_rgb(255, 255, 255)); // BR white

    // A 3px border proves the full extent is addressable — a panel with a row or
    // column offset shows it here as a missing or doubled edge.
    const uint16_t edge = display_rgb(255, 200, 0);
    display_fill_rect(0, 0, DISPLAY_WIDTH, 3, edge);
    display_fill_rect(0, DISPLAY_HEIGHT - 3, DISPLAY_WIDTH, 3, edge);
    display_fill_rect(0, 0, 3, DISPLAY_HEIGHT, edge);
    display_fill_rect(DISPLAY_WIDTH - 3, 0, 3, DISPLAY_HEIGHT, edge);

    // A greyscale ramp across the middle — banding here means the RGB565 packing
    // or byte order is off.
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        uint8_t v = (uint8_t)(x * 255 / (DISPLAY_WIDTH - 1));
        display_fill_rect(x, DISPLAY_HEIGHT / 2 - 20, 1, 40, display_rgb(v, v, v));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_flush());

    ESP_LOGI(TAG, "  expect: RED top-left, GREEN top-right, BLUE bottom-left,");
    ESP_LOGI(TAG, "          WHITE bottom-right, amber border, grey ramp centre");
    ESP_LOGI(TAG, "==== step 1 done — report what is on the glass ====");
}

// Step 2: touch. Draws where you press, which tests touch and display together
// and makes any calibration offset visible rather than a number to interpret.
static void step2_touch(void)
{
    ESP_LOGI(TAG, "==== step 2: touch ====");

    esp_err_t err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "  check wiring: T_CLK=18 T_DIN=17 T_DO=8 T_CS=15 T_IRQ=16");
        return;
    }

    // Calibrate when there is nothing stored, or when the panel is already being
    // held at boot — that press-to-recalibrate gesture is the recovery path once
    // the drift AGENTS.md warns about sets in.
    bool held_at_boot = touch_read(NULL, NULL, NULL, NULL);
    esp_err_t loaded = touch_load_calibration();

    if (loaded != ESP_OK || held_at_boot) {
        ESP_LOGI(TAG, "  %s — running calibration",
                 held_at_boot ? "panel held at boot" : "no stored calibration");
        if (touch_calibrate() != ESP_OK) {
            ESP_LOGE(TAG, "  calibration failed; coordinates will be raw ADC counts");
        }
    } else {
        ESP_LOGI(TAG, "  hold the panel while resetting to recalibrate");
    }

    ESP_LOGI(TAG, "  press the panel — dots follow your finger, coords go to the log");
    ESP_LOGI(TAG, "  resistive touch needs FIRM pressure; a light brush reads as nothing");

    display_fill(display_rgb(16, 16, 24));
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_flush());

    bool was_pressed = false;
    while (true) {
        int x = 0, y = 0;
        uint16_t rx = 0, ry = 0;

        if (touch_read(&x, &y, &rx, &ry)) {
            if (!was_pressed) {
                ESP_LOGI(TAG, "  press   x=%3d y=%3d (raw %u,%u)", x, y, rx, ry);
            }
            was_pressed = true;
            display_fill_rect(x - 4, y - 4, 9, 9, display_rgb(255, 120, 0));
            ESP_ERROR_CHECK_WITHOUT_ABORT(display_flush());
        } else if (was_pressed) {
            was_pressed = false;
            ESP_LOGI(TAG, "  release");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    // NVS holds the touch calibration. Erase and retry on the usual first-boot
    // and layout-change errors rather than failing the whole bring-up.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_err);

    // A blank line and banner make the start of a boot obvious in a noisy console.
    printf("\n");
    ESP_LOGI(TAG, "==== super-tamagotchi step 0: board bring-up ====");

    report_chip();
    bool psram_ok = report_psram();

    // esp_get_free_heap_size() counts PSRAM too once it joins the allocator, so it
    // reports ~8.5MB and reads as internal RAM at a glance. Internal is the scarce
    // pool and the one worth watching — ask for it explicitly.
    ESP_LOGI(TAG, "heap free : %zu KB internal, %zu KB total (internal + PSRAM)",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024);
    ESP_LOGI(TAG, "cpu       : %d MHz configured", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    // 320x240x16bpp = 150KB per framebuffer. Sanity-check the render budget now,
    // while it is cheap to discover a problem.
    ESP_LOGI(TAG, "budget    : a 320x240 16bpp framebuffer is %d KB; double-buffered %d KB",
             (320 * 240 * 2) / 1024, (320 * 240 * 2 * 2) / 1024);

    if (psram_ok) {
        ESP_LOGI(TAG, "==== step 0 PASSED ====");
    } else {
        ESP_LOGE(TAG, "==== step 0 FAILED — fix PSRAM before trusting anything below ====");
    }

    step1_display();
    step2_touch();  // does not return while touch is up

    // Heartbeat: proves the console link is live and the board has not reset.
    uint32_t tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive (%" PRIu32 ")", ++tick);
    }
}
