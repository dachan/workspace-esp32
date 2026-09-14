#pragma once

#include "esp_err.h"

esp_err_t st7735_init(void);
esp_err_t st7735_render(const char *date, const char *time_text);
