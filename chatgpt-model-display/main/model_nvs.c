#include "model_nvs.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "model_nvs";
static const char *NS = "cgpt";
static const char *KEY_MODEL = "model";
static const char *KEY_THINK = "think";

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

    nvs_close(h);
    if (out->has_model) {
        ESP_LOGI(TAG, "loaded cache model='%s' thinking='%s'",
                 out->model, out->has_thinking ? out->thinking : "-");
    }
    return out->has_model;
}

esp_err_t model_nvs_save(const model_fields_t *fields)
{
    if (!fields || !fields->has_model || fields->model[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
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
