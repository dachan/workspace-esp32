#pragma once

#include "esp_err.h"
#include "model_parse.h"

#include <stdbool.h>

// NVS namespace "cgpt": "model"/"think" are the last displayed pair;
// "effort" is the per-app, per-model thinking map.
esp_err_t model_nvs_init(void);
int model_nvs_load(model_fields_t *out);
esp_err_t model_nvs_save(const model_fields_t *fields);

void model_nvs_remember_effort_for(const model_fields_t *fields, bool cursor);
void model_nvs_remember_effort(const model_fields_t *fields);
void model_nvs_restore_effort_for(model_fields_t *fields, bool cursor);
void model_nvs_restore_effort(model_fields_t *fields);
