#pragma once

#include <stdint.h>

#include "canvas.h"
#include "esp_err.h"

esp_err_t display_init(void);
esp_err_t display_flush(void);
esp_err_t display_set_backlight(uint8_t percent);
