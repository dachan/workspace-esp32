#include "front_title.h"

#include <string.h>

static const char kChatGPT[] = "ChatGPT";
static const char kCursor[] = "Cursor";
static const char *s_title = kChatGPT;
static bool s_dirty;

bool front_title_apply_line(const char *line)
{
    const char *title;
    if (strcmp(line, "FRONT Cursor") == 0) {
        title = kCursor;
    } else if (strcmp(line, "FRONT ChatGPT") == 0) {
        title = kChatGPT;
    } else {
        return false;
    }
    if (s_title != title) {
        s_title = title;
        s_dirty = true;
    }
    return true;
}

const char *front_title_text(void)
{
    return s_title;
}

bool front_title_is_cursor(void)
{
    return s_title == kCursor;
}

bool front_title_needs_paint(void)
{
    return s_dirty;
}

void front_title_mark_drawn(void)
{
    s_dirty = false;
}
