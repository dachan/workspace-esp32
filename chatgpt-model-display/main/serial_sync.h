#pragma once

#include <stdbool.h>
#include "model_parse.h"

// Cache the desired state; local changes settle for 400 ms before sending.
void serial_sync_update(const model_fields_t *fields, bool local_change);
// Re-emit both fields with new revisions and a PUSH so the helper reapplies.
void serial_sync_push(const model_fields_t *fields);
bool serial_sync_handle_line(const char *line);
void serial_sync_poll(void);
