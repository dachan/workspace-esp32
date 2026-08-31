// FT6336G-family capacitive touch on the new 2.8-inch IPS display.
//
// The controller is factory calibrated and reports native panel pixels. The
// implementation rotates those into the display's 320x240 landscape space.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t touch_init(void);

// True while at least one contact is present. x/y are display coordinates.
// raw_x/raw_y expose the transformed driver values for diagnostic logging.
// Any output may be NULL.
bool touch_read(int *x, int *y, uint16_t *raw_x, uint16_t *raw_y);
