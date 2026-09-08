#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool down;
    bool pressed;
    bool released;
    int x;
    int y;
    int held_ms;
} touch_sample_t;

esp_err_t touch_init(void);
void touch_clear_state(void);
bool touch_raw(uint16_t *raw_x, uint16_t *raw_y);
bool touch_poll(touch_sample_t *ev);
