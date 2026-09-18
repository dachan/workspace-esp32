#pragma once
#include <stdbool.h>
#include <time.h>
typedef void (*clock_sync_time_saved_cb_t)(time_t value);
void clock_sync_init(clock_sync_time_saved_cb_t time_saved);
bool clock_sync_handle_command(const char *command, void (*write_line)(const char *));
