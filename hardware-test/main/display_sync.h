#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

esp_err_t display_sync_init(esp_lcd_panel_io_handle_t panel_io);
esp_err_t display_sync_draw(esp_lcd_panel_handle_t panel, int x_start, int y_start,
                            int x_end, int y_end, const void *color_data);
