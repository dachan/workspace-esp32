#pragma once

#include "esp_err.h"

typedef enum {
    ENCODER_THINKING = 0,
    ENCODER_MODEL = 1,
    ENCODER_COUNT = 2,
} encoder_id_t;

esp_err_t encoder_init(void);

/* -1 / 0 / +1 since last poll for that encoder. */
int encoder_delta(encoder_id_t id);

/* 1 on a fresh button press (active-low, debounced). */
int encoder_button_pressed(encoder_id_t id);
