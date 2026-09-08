#pragma once

#include <stdbool.h>

typedef enum { DESK_CHATGPT, DESK_CURSOR, DESK_OPENCODE } desk_app_t;
desk_app_t front_title_app(void);

bool front_title_apply_line(const char *line);
bool front_title_is_cursor(void);
bool front_title_is_focused(void);
bool front_title_needs_paint(void);
void front_title_mark_drawn(void);
