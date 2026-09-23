#include "serial_sync.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catalog.h"
#include "encoder.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_model.h"

typedef enum {
    SYNC_INTENT_NONE,
    SYNC_INTENT_APPLY,
    SYNC_INTENT_PUSH,
} sync_intent_t;

typedef struct {
    const char *kind;
    char value[MODEL_PARSE_MAX];
    uint64_t revision;
    bool pending;
    bool sent_once;
    TickType_t started_at;
    TickType_t wait_ticks;
} sync_field_t;

static sync_field_t s_fields[] = {{.kind = "MODEL"}, {.kind = "THINKING"}};
static sync_intent_t s_intent;
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
    field->sent_once = false;
    field->started_at = xTaskGetTickCount();
    field->wait_ticks = pdMS_TO_TICKS(400);
}

void serial_sync_update(const model_fields_t *fields, bool local_change)
{
    update_field(&s_fields[0], fields->has_model ? fields->model : "", local_change);
    update_field(&s_fields[1], fields->has_thinking ? fields->thinking : "", local_change);
}

static void stage_field(sync_field_t *field, const char *value, TickType_t wait_ticks)
{
    snprintf(field->value, sizeof(field->value), "%s", value);
    field->revision = ((uint64_t)esp_random() << 32) | esp_random();
    field->pending = value[0];
    field->sent_once = !value[0];
    field->started_at = xTaskGetTickCount();
    field->wait_ticks = wait_ticks;
}

static void stage_intent(
    const model_fields_t *fields, sync_intent_t intent, TickType_t wait_ticks
)
{
    stage_field(&s_fields[0], fields->has_model ? fields->model : "", wait_ticks);
    stage_field(&s_fields[1], fields->has_thinking ? fields->thinking : "", wait_ticks);
    s_intent = intent;
}

void serial_sync_apply(const model_fields_t *fields)
{
    stage_intent(fields, SYNC_INTENT_APPLY, pdMS_TO_TICKS(400));
}

void serial_sync_push(const model_fields_t *fields)
{
    stage_intent(fields, SYNC_INTENT_PUSH, 0);
}

void serial_sync_cancel_intent(void)
{
    s_intent = SYNC_INTENT_NONE;
}

static int parse_hex_bytes(const char *s, uint8_t *out, int max)
{
    int n = 0;
    while (n < max && s && *s) {
        while (*s == ' ') {
            s++;
        }
        if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) {
            break;
        }
        char buf[3] = { s[0], s[1], 0 };
        out[n++] = (uint8_t)strtoul(buf, NULL, 16);
        s += 2;
    }
    return n;
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
                field->sent_once = false;
            }
        }
        return true;
    }
    const char *older_prefix = "CONFIG CHATGPT_OLDER ";
    if (strncmp(line, older_prefix, strlen(older_prefix)) == 0) {
        const char *value = line + strlen(older_prefix);
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0) {
            bool show = value[0] == '1';
            s_config_changed |= show != catalog_chatgpt_show_older();
            catalog_chatgpt_set_show_older(show);
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
    const char *dial_swap_prefix = "CONFIG DIAL_SWAP ";
    if (strncmp(line, dial_swap_prefix, strlen(dial_swap_prefix)) == 0) {
        const char *value = line + strlen(dial_swap_prefix);
        if (value[0] == '0' || value[0] == '1') {
            encoder_set_swap(value[0] == '1');
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
    const char *rig_prefix = "CONFIG RIG_MODELS ";
    if (strncmp(line, rig_prefix, strlen(rig_prefix)) == 0) {
        char *end = NULL;
        const char *value = line + strlen(rig_prefix);
        uint64_t mask = strtoull(value, &end, 16);
        if (end != value && *end == '\0') {
            uint64_t before = catalog_rig_enabled_mask();
            catalog_rig_set_enabled_mask(mask);
            s_config_changed |= before != catalog_rig_enabled_mask();
        }
        return true;
    }
    if (strcmp(line, "CONFIG CHATGPT_CLEAR") == 0) {
        catalog_chatgpt_catalog_begin();
        return true;
    }
    const char *chatgpt_add = "CONFIG CHATGPT_ADD ";
    if (strncmp(line, chatgpt_add, strlen(chatgpt_add)) == 0) {
        const char *value = line + strlen(chatgpt_add);
        uint8_t mask[1];
        if (parse_hex_bytes(value, mask, 1) == 1) {
            const char *name = value;
            while (*name && !isspace((unsigned char)*name)) name++;
            while (*name && isspace((unsigned char)*name)) name++;
            if (*name) catalog_chatgpt_catalog_add(name, mask[0]);
        }
        return true;
    }
    if (strcmp(line, "CONFIG CHATGPT_END") == 0) {
        s_config_changed |= catalog_chatgpt_catalog_commit();
        return true;
    }
    if (strcmp(line, "CONFIG RIG_CLEAR") == 0) {
        catalog_rig_catalog_begin();
        return true;
    }
    const char *rig_add_prefix = "CONFIG RIG_ADD ";
    if (strncmp(line, rig_add_prefix, strlen(rig_add_prefix)) == 0) {
        const char *value = line + strlen(rig_add_prefix);
        uint8_t masks[1];
        int n = parse_hex_bytes(value, masks, 1);
        if (n == 1) {
            const char *name = value;
            while (*name && !isspace((unsigned char)*name)) {
                name++;
            }
            while (*name && isspace((unsigned char)*name)) {
                name++;
            }
            if (*name) {
                catalog_rig_catalog_add(name, masks[0]);
            }
        }
        return true;
    }
    if (strcmp(line, "CONFIG RIG_END") == 0) {
        catalog_rig_catalog_commit();
        s_config_changed = true;
        return true;
    }
    const char *rig_efforts_prefix = "CONFIG RIG_EFFORTS ";
    if (strncmp(line, rig_efforts_prefix, strlen(rig_efforts_prefix)) == 0) {
        uint8_t masks[16];
        int n = parse_hex_bytes(line + strlen(rig_efforts_prefix), masks, (int)sizeof(masks));
        if (n > 0) {
            catalog_rig_set_effort_masks(masks, n);
            s_config_changed = true;
        }
        return true;
    }
    const char *rig_host_prefix = "CONFIG RIG_HOST_EFFORTS ";
    if (strncmp(line, rig_host_prefix, strlen(rig_host_prefix)) == 0) {
        const char *value = line + strlen(rig_host_prefix);
        uint8_t masks[1];
        int n = parse_hex_bytes(value, masks, 1);
        if (n == 1) {
            const char *name = value;
            while (*name && !isspace((unsigned char)*name)) {
                name++;
            }
            while (*name && isspace((unsigned char)*name)) {
                name++;
            }
            catalog_rig_set_host_effort_mask(*name ? name : NULL, masks[0]);
            s_config_changed = true;
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
        if (sent) {
            field->sent_once = true;
        }
        // Legacy helpers remain usable until SYNC opts into acknowledgement.
        field->pending = s_acknowledged_protocol || !sent;
        field->started_at = now;
        field->wait_ticks = pdMS_TO_TICKS(sent ? 500 : 200);
    }

    if (s_intent != SYNC_INTENT_NONE) {
        bool state_sent = true;
        for (size_t i = 0; i < 2; i++) {
            if (s_fields[i].value[0] && !s_fields[i].sent_once) {
                state_sent = false;
            }
        }
        if (state_sent) {
            if (!s_acknowledged_protocol) {
                s_intent = SYNC_INTENT_NONE;
            } else if (serial_model_write_line(
                           s_intent == SYNC_INTENT_PUSH ? "PUSH" : "APPLY")) {
                s_intent = SYNC_INTENT_NONE;
            }
        }
    }
}
