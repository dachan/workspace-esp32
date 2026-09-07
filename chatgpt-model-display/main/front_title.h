#pragma once

#include <stdbool.h>

bool front_title_apply_line(const char *line);
const char *front_title_text(void);
bool front_title_is_cursor(void);
bool front_title_needs_paint(void);
void front_title_mark_drawn(void);
