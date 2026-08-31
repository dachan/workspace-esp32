// The drawing surface, with no hardware in it.
//
// Split out from display.h so the creature and the rasteriser depend only on a
// block of RGB565 pixels. That lets the exact same creature.c and gfx.c compile
// natively on a desktop (see sim/) and render to an image, so the artwork can be
// iterated on without flashing the board — and, more usefully, so it can be
// looked at by whoever is editing it.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240

// The active framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT RGB565 entries.
uint16_t *display_framebuffer(void);

// Points the drawing routines at a buffer. The ESP build passes its PSRAM
// framebuffer; the simulator passes malloc'd memory.
void canvas_set_framebuffer(uint16_t *fb);

void display_fill(uint16_t colour);
void display_fill_rect(int x, int y, int w, int h, uint16_t colour);

// Packs RGB565 *byte-swapped* for the panel. The ILI9341 takes pixels MSB-first
// while the ESP32 is little-endian, so a raw uint16_t would arrive with its
// bytes reversed — the classic "colours are nearly right but wrong" bug.
static inline uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return __builtin_bswap16(c);
}
