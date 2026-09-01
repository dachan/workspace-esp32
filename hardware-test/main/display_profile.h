#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#if defined(DISPLAY_PROFILE_ILI9341_2_8) || defined(DISPLAY_PROFILE_ILI9341_3_2)

#include "esp_lcd_ili9341.h"

#define LCD_NATIVE_WIDTH 240
#define LCD_NATIVE_HEIGHT 320
#define LCD_WIDTH LCD_NATIVE_HEIGHT
#define LCD_HEIGHT LCD_NATIVE_WIDTH

#if defined(DISPLAY_PROFILE_ILI9341_2_8)
#define DISPLAY_PROFILE_NAME "2.8-inch ILI9341"
#define DISPLAY_CALIBRATION_KEY "c28_land_ccw90"
#else
#define DISPLAY_PROFILE_NAME "3.2-inch ILI9341V / FT6336U"
#define DISPLAY_CALIBRATION_KEY "c32_land_ccw90"
#endif

static inline esp_err_t display_profile_create_panel(esp_lcd_panel_io_handle_t io,
                                                     const esp_lcd_panel_dev_config_t *config,
                                                     esp_lcd_panel_handle_t *panel)
{
    return esp_lcd_new_panel_ili9341(io, config, panel);
}

static const uint16_t display_calibration_targets[5][2] = {
    {20, 20}, {299, 20}, {299, 219}, {20, 219}, {160, 120},
};

#elif defined(DISPLAY_PROFILE_ST7796U_3_5)

#include "esp_lcd_st7796.h"

#define DISPLAY_PROFILE_NAME "3.5-inch ST7796U / FT6336U"
#define LCD_NATIVE_WIDTH 320
#define LCD_NATIVE_HEIGHT 480
#define LCD_WIDTH LCD_NATIVE_HEIGHT
#define LCD_HEIGHT LCD_NATIVE_WIDTH
#define DISPLAY_CALIBRATION_KEY "c35_land_ccw90"

static inline esp_err_t display_profile_create_panel(esp_lcd_panel_io_handle_t io,
                                                     const esp_lcd_panel_dev_config_t *config,
                                                     esp_lcd_panel_handle_t *panel)
{
    return esp_lcd_new_panel_st7796(io, config, panel);
}

static const uint16_t display_calibration_targets[5][2] = {
    {20, 20}, {459, 20}, {459, 299}, {20, 299}, {240, 160},
};

#else
#error "Select a display profile with DISPLAY_PROFILE."
#endif

// Native panel is portrait. Both radar boards are viewed landscape with the
// sensor at the physical top. MADCTL MV+MY is a 90° counter-clockwise
// rotation. After this, framebuffer +X is visual LEFT and +Y is visual down
// (x=0 is the viewed right edge). Draw every UI in this space. Do not change
// this MADCTL and do not software-transpose pixels — that mirrors glyphs.
// See AGENTS.md "Locked view mapping".
static inline esp_err_t display_profile_apply_view_orientation(esp_lcd_panel_handle_t panel)
{
    esp_err_t err = esp_lcd_panel_swap_xy(panel, true);
    if (err != ESP_OK) {
        return err;
    }
    return esp_lcd_panel_mirror(panel, false, true);
}

static inline void display_profile_touch_to_view(uint16_t raw_x, uint16_t raw_y,
                                                 uint16_t *view_x, uint16_t *view_y)
{
    uint16_t native_x = raw_x;
    uint16_t native_y = raw_y;
    if (raw_x >= LCD_NATIVE_WIDTH || raw_y >= LCD_NATIVE_HEIGHT) {
        native_x = (uint16_t)((uint32_t)raw_x * LCD_NATIVE_WIDTH / 4096);
        native_y = (uint16_t)((uint32_t)raw_y * LCD_NATIVE_HEIGHT / 4096);
    }
    *view_x = native_y;
    *view_y = (uint16_t)(LCD_NATIVE_WIDTH - 1 - native_x);
}
