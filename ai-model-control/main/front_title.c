#include "front_title.h"

#include <string.h>

static desk_app_t s_app;
static bool s_focused;
static bool s_dirty;

bool front_title_apply_line(const char *line)
{
    bool focused;
    desk_app_t app = s_app;
    if (strcmp(line, "FRONT Cursor") == 0) {
        focused = true;
        app = DESK_CURSOR;
    } else if (strcmp(line, "FRONT ChatGPT") == 0) {
        focused = true;
        app = DESK_CHATGPT;
    } else if (strcmp(line, "FRONT OpenCode") == 0) {
        focused = true;
        app = DESK_OPENCODE;
    } else if (strcmp(line, "FRONT None") == 0) {
        focused = false;
    } else {
        return false;
    }
    s_focused = focused;
    if (focused && s_app != app) {
        s_app = app;
        s_dirty = true;
    }
    return true;
}

bool front_title_is_cursor(void)
{
    return s_app == DESK_CURSOR;
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

desk_app_t front_title_app(void)
{
    return s_app;
}
