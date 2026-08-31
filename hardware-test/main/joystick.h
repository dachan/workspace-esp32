#pragma once

#include <stdbool.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

typedef struct {
    adc_oneshot_unit_handle_t adc;
    int center_x;
    int center_y;
    bool switch_latched;
} joystick_t;

typedef struct {
    int cursor_dx;
    int cursor_dy;
    bool switch_pressed;
} joystick_input_t;

esp_err_t joystick_init(joystick_t *joystick);
esp_err_t joystick_read(joystick_t *joystick, joystick_input_t *input);
