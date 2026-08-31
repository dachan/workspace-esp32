#pragma once

#include "esp_err.h"

// Drives the external 8-pixel WS2812B bar connected to GPIO1.
esp_err_t rainbow_wave_init(void);
void rainbow_wave_trigger(void);
void rainbow_wave_off(void);
void rainbow_wave_next_palette(void);
uint8_t rainbow_wave_palette_index(void);
uint8_t rainbow_wave_color_phase(void);
void rainbow_wave_step(void);
