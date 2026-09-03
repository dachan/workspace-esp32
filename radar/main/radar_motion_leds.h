#pragma once

#include <stdint.h>

#include "esp_err.h"

// Drives a moving green light crest on the GPIO1 WS2812B bar from radial acceleration.
esp_err_t radar_motion_leds_init(void);
void radar_motion_leds_update_acceleration(int16_t acceleration_mm_per_second_squared);
void radar_motion_leds_step(void);
