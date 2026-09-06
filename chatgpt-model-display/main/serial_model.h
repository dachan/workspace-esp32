#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "model_parse.h"

esp_err_t serial_model_init(void);

// Non-blocking: returns true when a complete line updated fields.
// Accepts:
//   MODEL <name>
//   THINKING <level>   (optional override)
int serial_model_poll(model_fields_t *fields);
