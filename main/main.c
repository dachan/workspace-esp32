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

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

void app_main(void)
{
    // A blank line and banner make the start of a boot obvious in a noisy console.
    printf("\n");
    ESP_LOGI(TAG, "==== super-tamagotchi step 0: board bring-up ====");

    report_chip();
    bool psram_ok = report_psram();

    ESP_LOGI(TAG, "heap free : %" PRIu32 " KB internal", esp_get_free_heap_size() / 1024);

    // 320x240x16bpp = 150KB per framebuffer. Sanity-check the render budget now,
    // while it is cheap to discover a problem.
    ESP_LOGI(TAG, "budget    : a 320x240 16bpp framebuffer is %d KB; double-buffered %d KB",
             (320 * 240 * 2) / 1024, (320 * 240 * 2 * 2) / 1024);

    if (psram_ok) {
        ESP_LOGI(TAG, "==== step 0 PASSED — safe to wire the display ====");
    } else {
        ESP_LOGE(TAG, "==== step 0 FAILED — fix PSRAM before wiring anything ====");
    }

    // Heartbeat: proves the console link is live and the board has not reset.
    uint32_t tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive (%" PRIu32 ")", ++tick);
    }
}
