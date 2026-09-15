#pragma once

#include <stdbool.h>
#include "model_parse.h"

// Cache the desired state; local changes settle for 400 ms before sending.
void serial_sync_update(const model_fields_t *fields, bool local_change);

// Publish a complete state followed by an explicit application intent.
// APPLY preserves the bridge's per-field cache; PUSH forces both fields.
void serial_sync_apply(const model_fields_t *fields);
void serial_sync_push(const model_fields_t *fields);
void serial_sync_cancel_intent(void);

void serial_sync_note_enabled(void);
bool serial_sync_handle_line(const char *line);
bool serial_sync_take_config_changed(void);
void serial_sync_poll(void);
