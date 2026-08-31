#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t microphone_init(void);
bool microphone_sound_detected(void);
esp_err_t microphone_read_pcm16(int16_t *samples, size_t sample_count,
                                size_t *samples_read, TickType_t timeout);
bool microphone_sound_detected_pcm(const int16_t *samples, size_t sample_count);
