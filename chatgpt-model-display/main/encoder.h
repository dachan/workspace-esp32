#pragma once

#include "esp_err.h"

typedef enum {
    ENCODER_THINKING = 0,
    ENCODER_MODEL = 1,
    ENCODER_COUNT = 2,
} encoder_id_t;

esp_err_t encoder_init(void);

/* Signed step count since last poll (thinking may be more than ±1). */
int encoder_delta(encoder_id_t id);

/* 1 while thinking pulses are still accumulating — skip SPI paint. */
int encoder_hold_paint(void);

/* 1 on a fresh button press (active-low, debounced). */
int encoder_button_pressed(encoder_id_t id);
