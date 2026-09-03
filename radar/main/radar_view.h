#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_lcd_panel_ops.h"

enum {
    RADAR_VIEW_MAX_PINGS = 5,
    RADAR_VIEW_MAX_PEOPLE = 3,
};

typedef struct {
    uint16_t x;
    uint16_t y;
    float age_seconds;
} radar_ping_t;

typedef struct {
    int16_t x_mm;
    int16_t y_mm;
    bool active;
} radar_person_t;

typedef struct {
    uint16_t cursor_x;
    uint16_t cursor_y;
    radar_person_t people[RADAR_VIEW_MAX_PEOPLE];
    float people_target_x[RADAR_VIEW_MAX_PEOPLE];
    float people_target_y[RADAR_VIEW_MAX_PEOPLE];
    bool people_target_active[RADAR_VIEW_MAX_PEOPLE];
    bool people_initialized[RADAR_VIEW_MAX_PEOPLE];
    radar_ping_t pings[RADAR_VIEW_MAX_PINGS];
    int16_t radial_acceleration_mm_per_second_squared;
    int64_t last_step_us;
} radar_view_t;

void radar_view_start(esp_lcd_panel_handle_t panel, radar_view_t *view);
void radar_view_restore(esp_lcd_panel_handle_t panel);
void radar_view_set_cursor(radar_view_t *view, int x, int y);
void radar_view_set_people(radar_view_t *view,
                           const radar_person_t people[RADAR_VIEW_MAX_PEOPLE],
                           int16_t radial_acceleration_mm_per_second_squared);
void radar_view_trigger_ping(radar_view_t *view);
void radar_view_step(esp_lcd_panel_handle_t panel, radar_view_t *view);
