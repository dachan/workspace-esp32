#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "calibrate.h"
#include "catalog.h"
#include "clock.h"
#include "display.h"
#include "encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_nvs.h"
#include "queue_status.h"
#include "serial_model.h"
#include "serial_sync.h"
#include "touch.h"
#include "ui.h"

#define CALIBRATE_HOLD_MS 5000
#define KNOWN_COMMIT_MS 2000

static const char *TAG = "chatgpt_model";

static bool apply_thinking_delta(model_fields_t *fields, int delta)
{
    if (!delta) {
        return false;
    }
    int level = catalog_thinking_level(fields->thinking);
    if (!level) {
        level = 2;
    }
    level += delta;
    if (level < 1) level = 1;
    if (level > THINKING_LEVEL_COUNT) level = THINKING_LEVEL_COUNT;
    const char *name = catalog_thinking_name(level);
    if (fields->has_thinking && strcmp(fields->thinking, name) == 0) {
        return false;
    }
    snprintf(fields->thinking, sizeof(fields->thinking), "%s", name);
    fields->has_thinking = 1;
    if (!fields->has_model) {
        snprintf(fields->model, sizeof(fields->model), "%s", "GPT-5.6 Luna");
        fields->has_model = 1;
    }
    return true;
}

static bool apply_model_delta(model_fields_t *fields, int delta)
{
    if (!delta) {
        return false;
    }
    int index = catalog_model_index(fields->model);
    if (index < 0) index = 0;
    index += delta;
    if (index < 0) index = 0;
    if (index >= catalog_model_count) index = catalog_model_count - 1;
    const char *name = catalog_models[index];
    if (fields->has_model && strcmp(fields->model, name) == 0) {
        return false;
    }
    snprintf(fields->model, sizeof(fields->model), "%s", name);
    fields->has_model = 1;
    if (!fields->has_thinking) {
        snprintf(fields->thinking, sizeof(fields->thinking), "%s", "Medium");
        fields->has_thinking = 1;
    }
    return true;
}

static bool same_fields(const model_fields_t *a, const model_fields_t *b)
{
    return a->has_model == b->has_model && a->has_thinking == b->has_thinking
        && strcmp(a->model, b->model) == 0 && strcmp(a->thinking, b->thinking) == 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "chatgpt-model-display starting");
    ESP_ERROR_CHECK(model_nvs_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_set_backlight(80));
    ESP_ERROR_CHECK(serial_model_init());
    ESP_ERROR_CHECK(encoder_init());
    if (touch_init() != ESP_OK) {
        ESP_LOGW(TAG, "touch unavailable; CANCEL still works via encoder click");
    }
    ESP_ERROR_CHECK(calibrate_init());

    model_fields_t fields = {0};
    bool save_pending = false;
    if (model_nvs_load(&fields)) {
        model_fields_t cached = fields;
        // Removed models migrate to the last dial entry; normalize legacy effort.
        if (catalog_model_index(fields.model) < 0) {
            snprintf(fields.model, sizeof(fields.model), "%s", catalog_models[catalog_model_count - 1]);
        }
        if (fields.has_thinking) {
            snprintf(fields.thinking, sizeof(fields.thinking), "%s",
                     catalog_thinking_name(catalog_thinking_level(cached.thinking)));
        }
        save_pending = !same_fields(&cached, &fields);
    }
    serial_sync_update(&fields, false);

    model_fields_t known = fields;
    bool known_dirty = false;
    TickType_t known_dirty_at = 0;
    bool hold_calibrated = false;
    bool paint_pending = true;
    bool hold_rx = false;
    TickType_t local_changed_at = 0;
    TickType_t last_save_attempt = 0;
    TickType_t last_paint_attempt = 0;
    bool save_failed = false;
    bool paint_failed = false;

    while (1) {
        model_fields_t before = fields;
        // Capture all inputs before persistence or SPI work blocks this task.
        int thinking_delta = encoder_delta(ENCODER_THINKING);
        int model_delta = encoder_delta(ENCODER_MODEL);
        bool thinking_pressed = encoder_button_pressed(ENCODER_THINKING);
        bool model_pressed = encoder_button_pressed(ENCODER_MODEL);
        touch_sample_t touch = {0};
        touch_poll(&touch);
        TickType_t now = xTaskGetTickCount();
        if (!touch.down) {
            hold_calibrated = false;
        }
        if (touch.down && touch.held_ms >= CALIBRATE_HOLD_MS && !hold_calibrated) {
            hold_calibrated = true;
            calibrate_run();
            touch_clear_state();
            paint_pending = true;
        }
        bool local_changed = apply_thinking_delta(&fields, thinking_delta);
        local_changed |= apply_model_delta(&fields, model_delta);
        bool cancel = queue_status_visible()
            && ((touch.released && touch.held_ms < CALIBRATE_HOLD_MS
                 && ui_cancel_hit(touch.x, touch.y))
                || thinking_pressed || model_pressed);
        if (cancel) {
            serial_model_write_line("CANCEL");
            queue_status_hide();
            fields = known;
            known_dirty = false;
            serial_sync_restore(&fields);
            thinking_pressed = false;
            model_pressed = false;
            local_changed = false;
        } else {
            if (thinking_pressed) {
                // Empty panel: the default Medium minus one selects Light.
                local_changed |= apply_thinking_delta(&fields, fields.has_thinking ? 1 : -1);
            }
            if (model_pressed) {
                local_changed |= apply_model_delta(&fields, 1);
            }
        }
        if (local_changed) {
            if (!known_dirty) {
                known = before;
                known_dirty = true;
                known_dirty_at = now;
            }
            hold_rx = true;
            local_changed_at = now;
            serial_sync_update(&fields, true);
        }

        // Compatibility display updates must not overwrite a settling local edit.
        // Protocol ACK/SYNC handling remains active throughout this hold window.
        if (hold_rx && (TickType_t)(now - local_changed_at) >= pdMS_TO_TICKS(8400)) {
            hold_rx = false;
        }
        model_fields_t incoming = fields;
        if (serial_model_poll(&incoming) && !hold_rx) {
            fields = incoming;
            serial_sync_update(&fields, false);
        }
        serial_sync_poll();
        if (!queue_status_visible() && !known_dirty) {
            known = fields;
        } else if (!queue_status_visible() && known_dirty
                   && (TickType_t)(now - known_dirty_at) >= pdMS_TO_TICKS(KNOWN_COMMIT_MS)) {
            known = fields;
            known_dirty = false;
        }

        if (!same_fields(&before, &fields)) {
            save_pending = fields.has_model;
            paint_pending = true;
        }
        if (clock_needs_paint() || queue_status_needs_paint()) {
            paint_pending = true;
        }
        now = xTaskGetTickCount();
        if (save_pending && (!save_failed || (TickType_t)(now - last_save_attempt) >= pdMS_TO_TICKS(1000))) {
            last_save_attempt = now;
            save_failed = model_nvs_save(&fields) != ESP_OK;
            save_pending = save_failed;
        }
        if (paint_pending && !encoder_hold_paint()
            && (!paint_failed || (TickType_t)(now - last_paint_attempt) >= pdMS_TO_TICKS(500))) {
            last_paint_attempt = now;
            paint_failed = ui_render(&fields) != ESP_OK;
            paint_pending = paint_failed;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
