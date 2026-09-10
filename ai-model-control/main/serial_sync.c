#include "serial_sync.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catalog.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_model.h"

typedef struct {
    const char *kind;
    char value[MODEL_PARSE_MAX];
    uint64_t revision;
    bool pending;
    TickType_t started_at;
    TickType_t wait_ticks;
} sync_field_t;

static sync_field_t s_fields[] = {{.kind = "MODEL"}, {.kind = "THINKING"}};
static bool s_acknowledged_protocol;
static bool s_send_enabled;
static bool s_config_changed;
static uint64_t s_boot_id;
static TickType_t s_ready_at;
static bool s_ready_sent;

void serial_sync_note_enabled(void)
{
    s_send_enabled = true;
}

static void update_field(sync_field_t *field, const char *value, bool local_change)
{
    if (strcmp(field->value, value) == 0) {
        return;
    }
    snprintf(field->value, sizeof(field->value), "%s", value);
    // Include a boot-independent identity so a device reset cannot reuse a Mac's
    // last acknowledged revision. This is an identity, not a security token.
    field->revision = ((uint64_t)esp_random() << 32) | esp_random();
    field->pending = local_change && value[0];
    field->started_at = xTaskGetTickCount();
    field->wait_ticks = pdMS_TO_TICKS(400);
}

void serial_sync_update(const model_fields_t *fields, bool local_change)
{
    update_field(&s_fields[0], fields->has_model ? fields->model : "", local_change);
    update_field(&s_fields[1], fields->has_thinking ? fields->thinking : "", local_change);
}

bool serial_sync_handle_line(const char *line)
{
    if (strcmp(line, "SYNC") == 0) {
        s_acknowledged_protocol = true;
        s_send_enabled = true;
        for (size_t i = 0; i < 2; i++) {
            sync_field_t *field = &s_fields[i];
            if (field->value[0] && !field->pending) {
                field->pending = true;
                field->wait_ticks = 0;
            }
        }
        return true;
    }
    const char *chatgpt_prefix = "CONFIG CHATGPT_EFFORTS ";
    if (strncmp(line, chatgpt_prefix, strlen(chatgpt_prefix)) == 0) {
        char *end = NULL;
        const char *value = line + strlen(chatgpt_prefix);
        uint64_t mask = strtoull(value, &end, 16);
        if (end != value && *end == '\0') {
            uint64_t before = catalog_chatgpt_thinking_mask();
            catalog_chatgpt_set_thinking_mask(mask);
            s_config_changed |= before != catalog_chatgpt_thinking_mask();
        }
        return true;
    }
    const char *cursor_prefix = "CONFIG CURSOR_MODELS ";
    if (strncmp(line, cursor_prefix, strlen(cursor_prefix)) == 0) {
        char *end = NULL;
        const char *value = line + strlen(cursor_prefix);
        uint64_t mask = strtoull(value, &end, 16);
        if (end != value && *end == '\0') {
            uint64_t before = catalog_cursor_enabled_mask();
            catalog_cursor_set_enabled_mask(mask);
            s_config_changed |= before != catalog_cursor_enabled_mask();
        }
        return true;
    }
    if (strncmp(line, "ACK ", 4) == 0) {
        char kind[9], extra;
        uint64_t revision;
        if (sscanf(line, "ACK %16" SCNx64 " %8s %c", &revision, kind, &extra) == 2) {
            for (size_t i = 0; i < 2; i++) {
                sync_field_t *field = &s_fields[i];
                if (strcmp(kind, field->kind) == 0 && revision == field->revision) {
                    field->pending = false;
                }
            }
        }
        return true;
    }
    return false;
}

bool serial_sync_take_config_changed(void)
{
    bool changed = s_config_changed;
    s_config_changed = false;
    return changed;
}

void serial_sync_poll(void)
{
    // Retry until SYNC so a late host or lost boot announcement still recovers.
    TickType_t ready_now = xTaskGetTickCount();
    if (!s_acknowledged_protocol &&
        (!s_ready_sent || (TickType_t)(ready_now - s_ready_at) >= pdMS_TO_TICKS(500))) {
        if (!s_boot_id) {
            s_boot_id = ((uint64_t)esp_random() << 32) | esp_random();
            if (!s_boot_id) { s_boot_id = 1; }
        }
        char ready[40];
        snprintf(ready, sizeof(ready), "READY %016" PRIx64, s_boot_id);
        serial_model_write_line(ready);
        s_ready_at = ready_now;
        s_ready_sent = true;
    }
    if (s_send_enabled) {
        char line[40];
        snprintf(line, sizeof(line), "ENABLED %016" PRIx64, catalog_cursor_enabled_mask());
        if (serial_model_write_line(line)) {
            s_send_enabled = false;
        }
    }
    for (size_t i = 0; i < 2; i++) {
        sync_field_t *field = &s_fields[i];
        TickType_t now = xTaskGetTickCount();
        if (!field->pending || (TickType_t)(now - field->started_at) < field->wait_ticks) {
            continue;
        }
        char line[SERIAL_LINE_MAX];
        if (s_acknowledged_protocol) {
            snprintf(line, sizeof(line), "STATE %016" PRIx64 " %s %s",
                     field->revision, field->kind, field->value);
        } else {
            snprintf(line, sizeof(line), "SET %s %s", field->kind, field->value);
        }
        bool sent = serial_model_write_line(line);
        // Legacy helpers remain usable until SYNC opts into acknowledgement.
        field->pending = s_acknowledged_protocol || !sent;
        field->started_at = now;
        field->wait_ticks = pdMS_TO_TICKS(sent ? 500 : 200);
    }
}
