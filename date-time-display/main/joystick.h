#pragma once

#include "esp_err.h"

typedef enum {
    JOYSTICK_NONE = 0,
    JOYSTICK_UP,
    JOYSTICK_DOWN,
    JOYSTICK_LEFT,
    JOYSTICK_RIGHT,
    JOYSTICK_MID,
    JOYSTICK_SEL,
    JOYSTICK_RST,
} joystick_action_t;

esp_err_t joystick_init(void);
joystick_action_t joystick_poll(void);
