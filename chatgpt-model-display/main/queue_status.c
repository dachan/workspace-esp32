#include "queue_status.h"

#include <string.h>

static bool s_visible;
static bool s_dirty;

bool queue_status_apply_line(const char *line)
{
    bool visible;
    if (strcmp(line, "QUEUED") == 0) {
        visible = true;
    } else if (strcmp(line, "CLEAR") == 0) {
        visible = false;
    } else {
        return false;
    }
    if (visible != s_visible) {
        s_visible = visible;
        s_dirty = true;
    }
    return true;
}

bool queue_status_visible(void)
{
    return s_visible;
}

bool queue_status_needs_paint(void)
{
    return s_dirty;
}

void queue_status_mark_drawn(void)
{
    s_dirty = false;
}

void queue_status_hide(void)
{
    if (s_visible) {
        s_visible = false;
        s_dirty = true;
    }
}
