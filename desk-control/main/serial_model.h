#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "model_parse.h"

#define SERIAL_LINE_MAX 192

esp_err_t serial_model_init(void);
// Handle ACK/SYNC plus optional legacy MODEL/THINKING display updates.
int serial_model_poll(model_fields_t *fields);
// Enqueue one complete framed line. Delivery is confirmed by protocol ACK.
bool serial_model_write_line(const char *line);
