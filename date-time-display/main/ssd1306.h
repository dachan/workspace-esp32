#pragma once

#include "esp_err.h"

esp_err_t ssd1306_init(void);
esp_err_t ssd1306_render(const char *date, const char *time_text);
