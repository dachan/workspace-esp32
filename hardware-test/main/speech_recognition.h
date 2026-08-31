#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    VOICE_COMMAND_NONE = 0,
    VOICE_COMMAND_UP,
    VOICE_COMMAND_DOWN,
    VOICE_COMMAND_LEFT,
    VOICE_COMMAND_RIGHT,
    VOICE_COMMAND_UP_RIGHT,
    VOICE_COMMAND_UP_LEFT,
    VOICE_COMMAND_DOWN_RIGHT,
    VOICE_COMMAND_DOWN_LEFT,
    VOICE_COMMAND_RESET,
} voice_command_t;

esp_err_t speech_recognition_init(void);
voice_command_t speech_recognition_take_command(void);
bool speech_recognition_take_sound_detected(void);
