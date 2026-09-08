#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "touch.h"

void cursor_settings_open(void);
void cursor_settings_close(void);
bool cursor_settings_is_open(void);
/* True when the frame should be redrawn. */
bool cursor_settings_handle(const touch_sample_t *touch, int model_delta, int think_delta,
                            bool model_pressed, bool think_pressed);
bool cursor_settings_take_mask_changed(void);
esp_err_t cursor_settings_render(void);
