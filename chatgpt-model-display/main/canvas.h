#pragma once

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240

uint16_t *display_framebuffer(void);
void canvas_set_framebuffer(uint16_t *fb);
void display_fill(uint16_t colour);
void display_fill_rect(int x, int y, int w, int h, uint16_t colour);

static inline uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return __builtin_bswap16(c);
}
