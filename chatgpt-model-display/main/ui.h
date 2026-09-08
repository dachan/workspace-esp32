#pragma once

#include "esp_err.h"
#include "model_parse.h"

esp_err_t ui_render(const model_fields_t *fields);
bool ui_hit_sync(int x, int y);
bool ui_hit_models(int x, int y);
/* Start the SYNC icon spin; call ui_sync_tick each loop and repaint when true. */
void ui_sync_pulse(void);
bool ui_sync_tick(void);
