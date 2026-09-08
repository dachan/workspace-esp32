#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t calibrate_init(void);
bool calibrate_map(uint16_t raw_x, uint16_t raw_y, int *x, int *y);
bool calibrate_run(void);
