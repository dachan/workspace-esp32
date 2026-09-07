#pragma once

#include <stdbool.h>

bool queue_status_apply_line(const char *line);
bool queue_status_visible(void);
bool queue_status_needs_paint(void);
void queue_status_mark_drawn(void);
void queue_status_hide(void);
