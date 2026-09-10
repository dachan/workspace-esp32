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
#include "front_title.h"
#include "model_nvs.h"
#include "serial_model.h"
#include "serial_sync.h"
#include "touch.h"
#include "ui.h"

#define CALIBRATE_HOLD_MS 5000
#define SCREENSAVER_IDLE_MS 60000

static const char *TAG = "chatgpt_model";

static bool apply_thinking_delta(model_fields_t *fields, int delta)
{
    if (!fields->has_model) {
        snprintf(fields->model, sizeof(fields->model), "%s", catalog_default_model());
        fields->has_model = 1;
    }
    if (!delta || catalog_thinking_count(fields->model) == 0) {
        return false;
    }
    int level = catalog_thinking_level(fields->model, fields->thinking);
    if (!level) {
        level = catalog_thinking_level(fields->model, catalog_default_thinking(fields->model));
    }
    level += delta;
    if (level < 1) level = 1;
    if (level > catalog_thinking_count(fields->model)) level = catalog_thinking_count(fields->model);
    const char *name = catalog_thinking_name(fields->model, level);
    if (fields->has_thinking && strcmp(fields->thinking, name) == 0) {
        return false;
    }
    snprintf(fields->thinking, sizeof(fields->thinking), "%s", name);
    fields->has_thinking = 1;
    return true;
}

static bool apply_model_delta(model_fields_t *fields, int delta)
{
    if (!delta) {
        return false;
    }
    int index = catalog_model_index(fields->model);
    if (index < 0) index = catalog_model_index(catalog_default_model());
    if (index < 0) index = 0;
    index += delta;
    if (index < 0) index = 0;
    if (index >= catalog_model_count()) index = catalog_model_count() - 1;
    const char *name = catalog_model_at(index);
    if (fields->has_model && strcmp(fields->model, name) == 0) {
        return false;
    }
    model_nvs_remember(fields);
    snprintf(fields->model, sizeof(fields->model), "%s", name);
    fields->has_model = 1;
    model_nvs_restore_effort(fields);
    if (!fields->has_thinking && catalog_thinking_count(fields->model) > 0) {
        snprintf(fields->thinking, sizeof(fields->thinking), "%s",
                 catalog_default_thinking(fields->model));
        fields->has_thinking = 1;
    }
    return true;
}

static bool clamp_cursor_model(model_fields_t *fields)
{
    if (!front_title_is_cursor() || !fields->has_model) {
        return false;
    }
    if (catalog_model_index(fields->model) >= 0) {
        return false;
    }
    snprintf(fields->model, sizeof(fields->model), "%s", catalog_default_model());
    fields->has_model = 1;
    model_nvs_restore_effort(fields);
    if (!fields->has_thinking && catalog_thinking_count(fields->model) > 0) {
        snprintf(fields->thinking, sizeof(fields->thinking), "%s",
                 catalog_default_thinking(fields->model));
        fields->has_thinking = 1;
    }
    return true;
}

static void adapt_fields_for_front(model_fields_t *fields)
{
    if (catalog_thinking_count(fields->model) == 0) return;
    int level = catalog_thinking_level(fields->model, fields->thinking);
    const char *name = catalog_thinking_name(fields->model, level);
    snprintf(fields->thinking, sizeof(fields->thinking), "%s", name);
    fields->has_thinking = 1;
}

static bool same_fields(const model_fields_t *a, const model_fields_t *b)
{
    return a->has_model == b->has_model && a->has_thinking == b->has_thinking
        && strcmp(a->model, b->model) == 0 && strcmp(a->thinking, b->thinking) == 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ai-model-control starting");
    ESP_ERROR_CHECK(model_nvs_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_set_backlight(80));
    ESP_ERROR_CHECK(serial_model_init());
    ESP_ERROR_CHECK(encoder_init());
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    ESP_LOGI(TAG, "round target: touch disabled; encoder clicks provide local controls");
#else
    if (touch_init() != ESP_OK) {
        ESP_LOGW(TAG, "touch unavailable; five-point calibration hold is disabled");
    }
#endif
    ESP_ERROR_CHECK(calibrate_init());

    model_fields_t fields = {0};
    bool save_pending = false;
    if (model_nvs_load(&fields)) {
        model_fields_t cached = fields;
        // Unknown to both catalogs migrate to the last ChatGPT dial entry.
        if (!catalog_model_known(fields.model)) {
            snprintf(fields.model, sizeof(fields.model), "%s", catalog_model_at(catalog_model_count() - 1));
        }
        if (fields.has_thinking && !catalog_thinking_known(fields.thinking)) {
            snprintf(fields.thinking, sizeof(fields.thinking), "%s",
                     catalog_thinking_name(fields.model, catalog_thinking_level(fields.model, cached.thinking)));
        }
        save_pending = !same_fields(&cached, &fields);
    } else {
        model_nvs_restore_for(&fields, DESK_CHATGPT);
        save_pending = fields.has_model;
    }
    serial_sync_update(&fields, false);

    desk_app_t previous_app = DESK_CHATGPT;
    bool front_ready = false;
    bool hold_calibrated = false;
    bool paint_pending = true;
    bool hold_rx = false;
    bool screensaver_on = false;
    TickType_t local_changed_at = 0;
    TickType_t last_save_attempt = 0;
    TickType_t last_paint_attempt = 0;
    TickType_t last_active = xTaskGetTickCount();
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
        bool local_changed = false;
        bool input_activity = thinking_delta || model_delta || thinking_pressed || model_pressed
            || touch.down || touch.released;
        local_changed |= apply_model_delta(&fields, model_delta);
        if (local_changed) adapt_fields_for_front(&fields);
        local_changed |= apply_thinking_delta(&fields, thinking_delta);
        if (thinking_pressed) {
            // Empty panel: Extra High minus one selects High.
            local_changed |= apply_thinking_delta(&fields, fields.has_thinking ? 1 : -1);
        }
        if (model_pressed) {
            local_changed |= apply_model_delta(&fields, 1);
        }
        if (local_changed) {
            adapt_fields_for_front(&fields);
            model_nvs_remember(&fields);
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
            adapt_fields_for_front(&fields);
            model_nvs_remember(&fields);
            serial_sync_update(&fields, false);
        }
        if (serial_sync_take_config_changed()) {
            clamp_cursor_model(&fields);
            adapt_fields_for_front(&fields);
            model_nvs_remember(&fields);
            save_pending = true;
            paint_pending = true;
        }
        serial_sync_poll();
        if (front_title_is_focused()) {
            desk_app_t app = front_title_app();
            if (!front_ready || app != previous_app) {
                if (front_ready) {
                    model_nvs_remember_for(&fields, previous_app);
                }
                model_nvs_restore_for(&fields, app);
                clamp_cursor_model(&fields);
                adapt_fields_for_front(&fields);
                model_nvs_remember_for(&fields, app);
                previous_app = app;
                front_ready = true;
                serial_sync_update(&fields, true);
            }
        }
        now = xTaskGetTickCount();
        if (front_title_is_focused() || input_activity) {
            last_active = now;
            if (screensaver_on) {
                screensaver_on = false;
                paint_pending = true;
            }
        } else if (!screensaver_on
                   && (TickType_t)(now - last_active) >= pdMS_TO_TICKS(SCREENSAVER_IDLE_MS)) {
            screensaver_on = true;
            paint_pending = true;
        }
        if (!same_fields(&before, &fields)) {
            save_pending = fields.has_model;
            paint_pending = true;
        }
        if (clock_needs_paint() || front_title_needs_paint()) {
            paint_pending = true;
        }
        now = xTaskGetTickCount();
        if (save_pending && (!save_failed || (TickType_t)(now - last_save_attempt) >= pdMS_TO_TICKS(1000))) {
            last_save_attempt = now;
            save_failed = model_nvs_save(&fields) != ESP_OK;
            save_pending = save_failed;
        }
        if (paint_pending && (screensaver_on || !encoder_hold_paint())
            && (!paint_failed || (TickType_t)(now - last_paint_attempt) >= pdMS_TO_TICKS(500))) {
            last_paint_attempt = now;
            if (screensaver_on) {
                paint_failed = ui_render_screensaver() != ESP_OK;
            } else {
                paint_failed = ui_render(&fields) != ESP_OK;
            }
            paint_pending = paint_failed;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
