#pragma once

#include <stddef.h>
#include <stdint.h>

/* Landscape view size for 3.5" ST7796U (native 320x480 portrait). */
#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 320

uint16_t *display_framebuffer(void);
void canvas_set_framebuffer(uint16_t *fb);
void display_fill(uint16_t colour);
void display_fill_rect(int x, int y, int w, int h, uint16_t colour);
/* Blend an 8-bit coverage mask (row-major, w bytes per row) from bg to fg. */
void display_blit_alpha(int x, int y, int w, int h, const uint8_t *alpha, uint16_t fg, uint16_t bg);

static inline uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return __builtin_bswap16(c);
}
