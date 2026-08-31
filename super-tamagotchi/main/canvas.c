#include "canvas.h"

static uint16_t *s_fb;

void canvas_set_framebuffer(uint16_t *fb)
{
    s_fb = fb;
}

uint16_t *display_framebuffer(void)
{
    return s_fb;
}

void display_fill_rect(int x, int y, int w, int h, uint16_t colour)
{
    if (s_fb == NULL) {
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_WIDTH)  { w = DISPLAY_WIDTH - x; }
    if (y + h > DISPLAY_HEIGHT) { h = DISPLAY_HEIGHT - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int row = y; row < y + h; row++) {
        uint16_t *line = s_fb + (size_t)row * DISPLAY_WIDTH + x;
        for (int col = 0; col < w; col++) {
            line[col] = colour;
        }
    }
}

void display_fill(uint16_t colour)
{
    display_fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, colour);
}
