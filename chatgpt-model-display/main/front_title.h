#pragma once

#include <stdbool.h>

bool front_title_apply_line(const char *line);
bool front_title_is_cursor(void);
bool front_title_needs_paint(void);
void front_title_mark_drawn(void);
