#pragma once

#include "esp_err.h"

/* KY-040-style rotary encoder → thinking level (1..4). */
esp_err_t encoder_init(void);

/* Returns -1 / 0 / +1 since last poll (detents). */
int encoder_delta(void);

/* Returns 1 on a fresh button press (active-low, debounced). */
int encoder_button_pressed(void);
