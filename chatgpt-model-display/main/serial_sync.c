#include "serial_sync.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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
static bool s_hold_sync;

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
    if (local_change) {
        s_hold_sync = false;
    }
    update_field(&s_fields[0], fields->has_model ? fields->model : "", local_change);
    update_field(&s_fields[1], fields->has_thinking ? fields->thinking : "", local_change);
}

void serial_sync_restore(const model_fields_t *fields)
{
    s_hold_sync = true;
    update_field(&s_fields[0], fields->has_model ? fields->model : "", false);
    update_field(&s_fields[1], fields->has_thinking ? fields->thinking : "", false);
    s_fields[0].pending = false;
    s_fields[1].pending = false;
}

bool serial_sync_handle_line(const char *line)
{
    if (strcmp(line, "SYNC") == 0) {
        s_acknowledged_protocol = true;
        if (s_hold_sync) {
            return true;
        }
        for (size_t i = 0; i < 2; i++) {
            sync_field_t *field = &s_fields[i];
            if (field->value[0] && !field->pending) {
                field->pending = true;
                field->wait_ticks = 0;
            }
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

void serial_sync_poll(void)
{
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
