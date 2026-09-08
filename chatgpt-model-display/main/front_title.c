#include "front_title.h"

#include <string.h>

static bool s_is_cursor;
static bool s_dirty;
static bool s_received;

bool front_title_apply_line(const char *line)
{
    bool is_cursor;
    if (strcmp(line, "FRONT Cursor") == 0) {
        is_cursor = true;
    } else if (strcmp(line, "FRONT ChatGPT") == 0) {
        is_cursor = false;
    } else {
        return false;
    }
    s_received = true;
    if (s_is_cursor != is_cursor) {
        s_is_cursor = is_cursor;
        s_dirty = true;
    }
    return true;
}

bool front_title_is_cursor(void)
{
    return s_is_cursor;
}

bool front_title_received(void)
{
    return s_received;
}

bool front_title_needs_paint(void)
{
    return s_dirty;
}

void front_title_mark_drawn(void)
{
    s_dirty = false;
}
