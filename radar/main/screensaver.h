#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_lcd_panel_ops.h"

enum {
    SCREENSAVER_MAX_RIPPLES = 5,
    SCREENSAVER_MAX_PEOPLE = 3,
};

typedef struct {
    uint16_t x;
    uint16_t y;
    float age_seconds;
    bool active;
    bool radar_ping;
} screensaver_ripple_t;

typedef struct {
    int16_t x_mm;
    int16_t y_mm;
    bool active;
} screensaver_person_t;

typedef struct {
    unsigned int hue;
    uint8_t palette_index;
    uint8_t palette_phase;
    uint16_t cursor_x;
    uint16_t cursor_y;
    screensaver_person_t people[SCREENSAVER_MAX_PEOPLE];
    float people_target_x[SCREENSAVER_MAX_PEOPLE];
    float people_target_y[SCREENSAVER_MAX_PEOPLE];
    bool people_target_active[SCREENSAVER_MAX_PEOPLE];
    bool people_initialized[SCREENSAVER_MAX_PEOPLE];
    screensaver_ripple_t ripples[SCREENSAVER_MAX_RIPPLES];
    int8_t active_ripple;
    bool touch_active;
    bool radar_connected;
    int64_t last_step_us;
} screensaver_t;

void screensaver_start(esp_lcd_panel_handle_t panel, screensaver_t *screensaver);
void screensaver_set_palette(screensaver_t *screensaver, uint8_t palette_index,
                             uint8_t palette_phase);
void screensaver_set_cursor(screensaver_t *screensaver, int x, int y);
void screensaver_set_people(screensaver_t *screensaver,
                            const screensaver_person_t people[SCREENSAVER_MAX_PEOPLE],
                            bool radar_connected);
void screensaver_trigger_radar_ping(screensaver_t *screensaver);
void screensaver_step(esp_lcd_panel_handle_t panel, screensaver_t *screensaver);
void screensaver_touch(screensaver_t *screensaver, uint16_t x, uint16_t y, bool touched);
