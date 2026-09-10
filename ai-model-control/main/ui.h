#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "model_parse.h"

esp_err_t ui_render(const model_fields_t *fields);
esp_err_t ui_render_screensaver(void);
bool ui_hit_sync(int x, int y);
/* Start the SYNC icon spin; call ui_sync_tick each loop and repaint when true. */
void ui_sync_pulse(void);
bool ui_sync_tick(void);
