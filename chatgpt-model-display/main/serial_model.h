#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "model_parse.h"

esp_err_t serial_model_init(void);

// Mac → ESP display updates (never sent by the ESP):
//   MODEL <name>
//   THINKING <level>
// ESP → Mac commands (Mac applies these; ESP ignores them on RX):
//   SET MODEL <name>
//   SET THINKING <level>
int serial_model_poll(model_fields_t *fields);

// Write one SET line to USB Serial/JTAG. Returns true if the write completed.
int serial_model_send_set_model(const char *name);
int serial_model_send_set_thinking(const char *level);
