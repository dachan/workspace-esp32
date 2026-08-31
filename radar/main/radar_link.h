#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

enum { RADAR_LINK_MAX_TARGETS = 3 };

typedef struct {
    int16_t x_mm;
    int16_t y_mm;
    bool active;
} radar_link_target_t;

typedef struct {
    radar_link_target_t targets[RADAR_LINK_MAX_TARGETS];
} radar_link_frame_t;

esp_err_t radar_link_init(void);
esp_err_t radar_link_send(const radar_link_frame_t *frame);
bool radar_link_receive(radar_link_frame_t *frame);
const char *radar_link_role_name(void);
