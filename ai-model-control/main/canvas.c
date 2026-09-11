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
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > DISPLAY_WIDTH) {
        w = DISPLAY_WIDTH - x;
    }
    if (y + h > DISPLAY_HEIGHT) {
        h = DISPLAY_HEIGHT - y;
    }
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

void display_fill_round_rect(int x, int y, int w, int h, int radius, uint16_t colour)
{
    if (s_fb == NULL || w <= 0 || h <= 0) {
        return;
    }
    int r = radius;
    const int max_r = (w < h ? w : h) / 2;
    if (r > max_r) {
        r = max_r;
    }
    if (r < 1) {
        display_fill_rect(x, y, w, h, colour);
        return;
    }

    /* Pixel-center test against quarter-circles of radius r.
     * 2x coordinates avoid floats: pixel i is at 2*i+1, circle at 2*r. */
    const int rr4 = 4 * r * r;
    for (int row = 0; row < h; row++) {
        int u = row;
        if (h - 1 - row < u) {
            u = h - 1 - row;
        }
        int inset = 0;
        if (u < r) {
            const int dy2 = 2 * r - 2 * u - 1;
            while (inset < r) {
                const int dx2 = 2 * inset + 1 - 2 * r;
                if (dx2 * dx2 + dy2 * dy2 <= rr4) {
                    break;
                }
                inset++;
            }
        }
        const int span = w - 2 * inset;
        if (span > 0) {
            display_fill_rect(x + inset, y + row, span, 1, colour);
        }
    }
}

static uint8_t mix(uint8_t fg, uint8_t bg, uint8_t a)
{
    return (uint8_t)((fg * a + bg * (255 - a) + 127) / 255);
}

/* Framebuffer words are byte-swapped for the panel, so unswap before mixing. */
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a)
{
    const uint16_t f = __builtin_bswap16(fg);
    const uint16_t b = __builtin_bswap16(bg);
    const uint16_t r = mix((f >> 11) & 0x1F, (b >> 11) & 0x1F, a);
    const uint16_t g = mix((f >> 5) & 0x3F, (b >> 5) & 0x3F, a);
    const uint16_t blue = mix(f & 0x1F, b & 0x1F, a);
    return __builtin_bswap16((uint16_t)((r << 11) | (g << 5) | blue));
}

void display_blit_alpha(int x, int y, int w, int h, const uint8_t *alpha, uint16_t fg, uint16_t bg)
{
    if (s_fb == NULL || alpha == NULL) {
        return;
    }
    const int stride = w;
    int src_x = 0;
    int src_y = 0;
    if (x < 0) {
        src_x = -x;
        w += x;
        x = 0;
    }
    if (y < 0) {
        src_y = -y;
        h += y;
        y = 0;
    }
    if (x + w > DISPLAY_WIDTH) {
        w = DISPLAY_WIDTH - x;
    }
    if (y + h > DISPLAY_HEIGHT) {
        h = DISPLAY_HEIGHT - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int row = 0; row < h; row++) {
        uint16_t *line = s_fb + (size_t)(y + row) * DISPLAY_WIDTH + x;
        const uint8_t *src = alpha + (size_t)(src_y + row) * stride + src_x;
        for (int col = 0; col < w; col++) {
            const uint8_t a = src[col];
            if (a == 0) {
                line[col] = bg;
            } else if (a == 255) {
                line[col] = fg;
            } else {
                line[col] = blend565(fg, bg, a);
            }
        }
    }
}
