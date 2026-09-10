#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "model_parse.h"

esp_err_t ui_render(const model_fields_t *fields);
esp_err_t ui_render_screensaver(void);
