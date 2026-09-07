#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "model_parse.h"

esp_err_t serial_model_init(void);

// Non-blocking: returns true when a complete line updated fields.
// Accepts from Mac:
//   MODEL <name>
//   THINKING <level>   (optional override)
int serial_model_poll(model_fields_t *fields);

// ESP → Mac (encoder changes). Mac must not echo these back as MODEL lines
// in a tight loop; use SET prefix so poll() ignores them.
esp_err_t serial_model_send_set_model(const char *model);
esp_err_t serial_model_send_set_thinking(const char *thinking);
