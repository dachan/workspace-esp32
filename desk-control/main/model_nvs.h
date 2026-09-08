#pragma once

#include "esp_err.h"
#include "model_parse.h"
#include "front_title.h"

#include <stdbool.h>

// NVS namespace "cgpt": "model"/"think" are the last displayed pair;
// "last_g"/"last_c"/"last_o" are each app's last model; "effort" is per-app per-model thinking;
// "c_en" is the Cursor dial enable mask.
esp_err_t model_nvs_init(void);
int model_nvs_load(model_fields_t *out);
esp_err_t model_nvs_save(const model_fields_t *fields);

void model_nvs_remember_for(const model_fields_t *fields, desk_app_t app);
void model_nvs_remember(const model_fields_t *fields);
void model_nvs_restore_for(model_fields_t *fields, desk_app_t app);
void model_nvs_restore_effort(model_fields_t *fields);
