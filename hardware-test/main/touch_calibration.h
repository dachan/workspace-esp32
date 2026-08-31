#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

typedef bool (*touch_read_fn_t)(uint16_t *raw_x, uint16_t *raw_y);
typedef void (*touch_hold_fn_t)(void);

typedef struct {
    float x_offset;
    float x_from_raw_x;
    float x_from_raw_y;
    float y_offset;
    float y_from_raw_x;
    float y_from_raw_y;
} touch_calibration_t;

bool touch_calibration_run(esp_lcd_panel_handle_t panel, touch_read_fn_t touch_read,
                           touch_hold_fn_t touch_hold, gpio_num_t backlight_pin,
                           touch_calibration_t *result);
bool touch_calibration_load(touch_calibration_t *result);
void touch_calibration_apply(const touch_calibration_t *calibration, uint16_t raw_x,
                             uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y);
