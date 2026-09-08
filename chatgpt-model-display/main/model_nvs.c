#include "model_nvs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "catalog.h"
#include "esp_log.h"
#include "front_title.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "model_nvs";
static const char *NS = "cgpt";
static const char *KEY_MODEL = "model";
static const char *KEY_THINK = "think";
static const char *KEY_LAST_G = "last_g";
static const char *KEY_LAST_C = "last_c";
static const char *KEY_EFFORT = "effort";

#define EFFORT_BLOB_VER 1
#define EFFORT_SLOT_MAX 16
#define EFFORT_MODEL_MAX 32
#define EFFORT_THINK_MAX 16

typedef struct __attribute__((packed)) {
    uint8_t cursor;
    char model[EFFORT_MODEL_MAX];
    char thinking[EFFORT_THINK_MAX];
} effort_slot_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t count;
    effort_slot_t slots[EFFORT_SLOT_MAX];
} effort_blob_t;

static effort_blob_t s_effort;
static char s_last_model[2][EFFORT_MODEL_MAX];

static void copy_trunc(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_sz) {
        n = dst_sz - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int find_slot(bool cursor, const char *model)
{
    if (!model || !model[0]) {
        return -1;
    }
    for (int i = 0; i < s_effort.count; i++) {
        if ((bool)s_effort.slots[i].cursor == cursor
            && strcasecmp(s_effort.slots[i].model, model) == 0) {
            return i;
        }
    }
    return -1;
}

static void seed_if_empty(const model_fields_t *fields, bool cursor)
{
    if (find_slot(cursor, fields->model) >= 0) {
        return;
    }
    model_nvs_remember_for(fields, cursor);
}

static void load_last_model(nvs_handle_t h, const char *key, char *out, size_t out_sz)
{
    size_t len = out_sz;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) {
        out[0] = '\0';
    }
}

static void seed_last_model(const model_fields_t *fields, bool cursor)
{
    char *slot = s_last_model[cursor ? 1 : 0];
    if (slot[0] || !fields->has_model) {
        return;
    }
    if (catalog_model_index_in(cursor, fields->model) >= 0) {
        copy_trunc(slot, EFFORT_MODEL_MAX, fields->model);
    }
}

esp_err_t model_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

int model_nvs_load(model_fields_t *out)
{
    memset(out, 0, sizeof(*out));
    memset(&s_effort, 0, sizeof(s_effort));
    memset(s_last_model, 0, sizeof(s_last_model));
    s_effort.version = EFFORT_BLOB_VER;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return 0;
    }

    size_t len = sizeof(out->model);
    err = nvs_get_str(h, KEY_MODEL, out->model, &len);
    if (err == ESP_OK && out->model[0] != '\0') {
        out->has_model = 1;
    }

    len = sizeof(out->thinking);
    err = nvs_get_str(h, KEY_THINK, out->thinking, &len);
    if (err == ESP_OK && out->thinking[0] != '\0') {
        out->has_thinking = 1;
    }

    effort_blob_t blob = {0};
    len = sizeof(blob);
    err = nvs_get_blob(h, KEY_EFFORT, &blob, &len);
    if (err == ESP_OK && blob.version == EFFORT_BLOB_VER && blob.count <= EFFORT_SLOT_MAX
        && len >= 2) {
        s_effort = blob;
        s_effort.version = EFFORT_BLOB_VER;
    }
    load_last_model(h, KEY_LAST_G, s_last_model[0], sizeof(s_last_model[0]));
    load_last_model(h, KEY_LAST_C, s_last_model[1], sizeof(s_last_model[1]));

    nvs_close(h);
    if (out->has_model) {
        seed_if_empty(out, false);
        seed_if_empty(out, true);
        seed_last_model(out, false);
        seed_last_model(out, true);
        ESP_LOGI(TAG, "loaded cache model='%s' thinking='%s' efforts=%u last_g='%s' last_c='%s'",
                 out->model, out->has_thinking ? out->thinking : "-", s_effort.count,
                 s_last_model[0][0] ? s_last_model[0] : "-",
                 s_last_model[1][0] ? s_last_model[1] : "-");
    }
    return out->has_model;
}

esp_err_t model_nvs_save(const model_fields_t *fields)
{
    if (!fields || !fields->has_model || fields->model[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    model_nvs_remember(fields);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, KEY_MODEL, fields->model);
    if (err == ESP_OK) {
        if (fields->has_thinking && fields->thinking[0] != '\0') {
            err = nvs_set_str(h, KEY_THINK, fields->thinking);
        } else {
            // Clear stale thinking so boot does not show an old level.
            err = nvs_erase_key(h, KEY_THINK);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
            }
        }
    }
    if (err == ESP_OK) {
        err = s_last_model[0][0] ? nvs_set_str(h, KEY_LAST_G, s_last_model[0])
                                 : nvs_erase_key(h, KEY_LAST_G);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = s_last_model[1][0] ? nvs_set_str(h, KEY_LAST_C, s_last_model[1])
                                 : nvs_erase_key(h, KEY_LAST_C);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        s_effort.version = EFFORT_BLOB_VER;
        err = nvs_set_blob(h, KEY_EFFORT, &s_effort, sizeof(s_effort));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved cache model='%s' thinking='%s'",
                 fields->model, fields->has_thinking ? fields->thinking : "-");
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

void model_nvs_remember_for(const model_fields_t *fields, bool cursor)
{
    if (!fields || !fields->has_model || fields->model[0] == '\0') {
        return;
    }
    if (catalog_model_index_in(cursor, fields->model) < 0) {
        return;
    }
    copy_trunc(s_last_model[cursor ? 1 : 0], EFFORT_MODEL_MAX, fields->model);
    if (!fields->has_thinking || fields->thinking[0] == '\0') {
        return;
    }
    if (catalog_thinking_count_in(cursor, fields->model) == 0) {
        return;
    }

    int slot = find_slot(cursor, fields->model);
    if (slot < 0) {
        if (s_effort.count >= EFFORT_SLOT_MAX) {
            return;
        }
        slot = s_effort.count++;
        s_effort.slots[slot].cursor = cursor ? 1 : 0;
        copy_trunc(s_effort.slots[slot].model, sizeof(s_effort.slots[slot].model), fields->model);
    }
    copy_trunc(s_effort.slots[slot].thinking, sizeof(s_effort.slots[slot].thinking),
               fields->thinking);
}

void model_nvs_remember(const model_fields_t *fields)
{
    model_nvs_remember_for(fields, front_title_is_cursor());
}

void model_nvs_restore_for(model_fields_t *fields, bool cursor)
{
    if (!fields) {
        return;
    }
    const char *saved = s_last_model[cursor ? 1 : 0];
    if (saved[0] && catalog_model_index_in(cursor, saved) >= 0) {
        snprintf(fields->model, sizeof(fields->model), "%s", saved);
        fields->has_model = 1;
    } else if (!fields->has_model || catalog_model_index_in(cursor, fields->model) < 0) {
        snprintf(fields->model, sizeof(fields->model), "%s", catalog_model_at_in(cursor, 0));
        fields->has_model = 1;
    }
    if (catalog_thinking_count_in(cursor, fields->model) == 0) {
        return;
    }
    int slot = find_slot(cursor, fields->model);
    if (slot >= 0 && s_effort.slots[slot].thinking[0]) {
        snprintf(fields->thinking, sizeof(fields->thinking), "%s", s_effort.slots[slot].thinking);
        fields->has_thinking = 1;
        return;
    }
    if (!fields->has_thinking) {
        snprintf(fields->thinking, sizeof(fields->thinking), "%s", "Medium");
        fields->has_thinking = 1;
    }
}

void model_nvs_restore_effort(model_fields_t *fields)
{
    if (!fields || !fields->has_model) {
        return;
    }
    bool cursor = front_title_is_cursor();
    if (catalog_thinking_count_in(cursor, fields->model) == 0) {
        return;
    }
    int slot = find_slot(cursor, fields->model);
    if (slot < 0 || s_effort.slots[slot].thinking[0] == '\0') {
        return;
    }
    snprintf(fields->thinking, sizeof(fields->thinking), "%s", s_effort.slots[slot].thinking);
    fields->has_thinking = 1;
}
