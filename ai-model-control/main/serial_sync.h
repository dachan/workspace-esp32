#pragma once

#include <stdbool.h>
#include "model_parse.h"

// Cache the desired state; local changes settle for 400 ms before sending.
void serial_sync_update(const model_fields_t *fields, bool local_change);
void serial_sync_note_enabled(void);
bool serial_sync_handle_line(const char *line);
bool serial_sync_take_config_changed(void);
void serial_sync_poll(void);
