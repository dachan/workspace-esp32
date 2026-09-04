#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

enum {
    RECEIVER_TOUCH_INT_GPIO = GPIO_NUM_5,
    RECEIVER_TOUCH_SDA_GPIO = GPIO_NUM_6,
    RECEIVER_TOUCH_RESET_GPIO = GPIO_NUM_7,
    RECEIVER_TOUCH_SCL_GPIO = GPIO_NUM_15,
};

esp_err_t receiver_touch_init(void);
bool receiver_touch_read(uint16_t *raw_x, uint16_t *raw_y);
