#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

enum { LD2450_MAX_TARGETS = 3 };

typedef struct {
    int16_t x_mm;
    int16_t y_mm;
    int16_t speed_cm_s;
    uint16_t resolution_mm;
    bool active;
} ld2450_target_t;

typedef struct {
    ld2450_target_t targets[LD2450_MAX_TARGETS];
} ld2450_frame_t;

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint32_t build;
} ld2450_firmware_version_t;

esp_err_t ld2450_init(void);
esp_err_t ld2450_set_single_target_mode(void);
esp_err_t ld2450_get_firmware_version(ld2450_firmware_version_t *version);
bool ld2450_poll(ld2450_frame_t *frame);
