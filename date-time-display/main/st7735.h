#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t st7735_init(void);
esp_err_t st7735_render_rainbow(uint8_t phase);
