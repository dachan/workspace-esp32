#pragma once

#include "esp_err.h"
#include "model_parse.h"

// NVS namespace "cgpt", keys: "model", "think" (strings).
esp_err_t model_nvs_init(void);
int model_nvs_load(model_fields_t *out);
esp_err_t model_nvs_save(const model_fields_t *fields);
