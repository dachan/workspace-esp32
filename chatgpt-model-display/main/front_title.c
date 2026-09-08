#include "front_title.h"

#include <string.h>

static bool s_is_cursor;
static bool s_focused;
static bool s_dirty;

bool front_title_apply_line(const char *line)
{
    bool focused;
    bool is_cursor = s_is_cursor;
    if (strcmp(line, "FRONT Cursor") == 0) {
        focused = true;
        is_cursor = true;
    } else if (strcmp(line, "FRONT ChatGPT") == 0) {
        focused = true;
        is_cursor = false;
    } else if (strcmp(line, "FRONT None") == 0) {
        focused = false;
    } else {
        return false;
    }
    s_focused = focused;
    if (focused && s_is_cursor != is_cursor) {
        s_is_cursor = is_cursor;
        s_dirty = true;
    }
    return true;
}

bool front_title_is_cursor(void)
{
    return s_is_cursor;
}

bool front_title_is_focused(void)
{
    return s_focused;
}

bool front_title_needs_paint(void)
{
    return s_dirty;
}

void front_title_mark_drawn(void)
{
    s_dirty = false;
}
