#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#if defined(DISPLAY_PROFILE_ILI9341_2_8) || defined(DISPLAY_PROFILE_ILI9341_3_2)

#include "esp_lcd_ili9341.h"

#define LCD_WIDTH 240
#define LCD_HEIGHT 320

#if defined(DISPLAY_PROFILE_ILI9341_2_8)
#define DISPLAY_PROFILE_NAME "2.8-inch ILI9341"
#define DISPLAY_CALIBRATION_KEY "c28_ft6336"
#else
#define DISPLAY_PROFILE_NAME "3.2-inch ILI9341V / FT6336U"
#define DISPLAY_CALIBRATION_KEY "calibration_240x320_3_2"
#endif

static inline esp_err_t display_profile_create_panel(esp_lcd_panel_io_handle_t io,
                                                     const esp_lcd_panel_dev_config_t *config,
                                                     esp_lcd_panel_handle_t *panel)
{
    return esp_lcd_new_panel_ili9341(io, config, panel);
}

static const uint16_t display_calibration_targets[5][2] = {
    {20, 20}, {219, 20}, {219, 299}, {20, 299}, {120, 160},
};

#elif defined(DISPLAY_PROFILE_ST7796U_3_5)

#include "esp_lcd_st7796.h"

#define DISPLAY_PROFILE_NAME "3.5-inch ST7796U / FT6336U"
#define LCD_WIDTH 320
#define LCD_HEIGHT 480
#define DISPLAY_CALIBRATION_KEY "calibration_320x480"

static inline esp_err_t display_profile_create_panel(esp_lcd_panel_io_handle_t io,
                                                     const esp_lcd_panel_dev_config_t *config,
                                                     esp_lcd_panel_handle_t *panel)
{
    return esp_lcd_new_panel_st7796(io, config, panel);
}

static const uint16_t display_calibration_targets[5][2] = {
    {20, 20}, {299, 20}, {299, 459}, {20, 459}, {160, 240},
};

#else
#error "Select a display profile with DISPLAY_PROFILE."
#endif
