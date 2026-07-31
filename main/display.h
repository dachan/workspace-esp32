// Display bring-up: ILI9341 over SPI, with the framebuffer living in PSRAM.
//
// The PSRAM framebuffer is not incidental — it is the rendering architecture the
// pet needs. 320x240x16bpp is 150KB, trivial against 8MB, so the creature can be
// composed off-screen and blitted whole rather than drawn in fiddly dirty rects.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240

// Brings up SPI, the panel, and the backlight, and allocates the PSRAM
// framebuffer. Safe to call once.
esp_err_t display_init(void);

// Pointer to the RGB565 framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT entries.
uint16_t *display_framebuffer(void);

// Pushes the whole framebuffer to the panel.
esp_err_t display_flush(void);

// Backlight duty, 0-100. The backlight is the largest power draw in the device,
// so this is a power control as much as a comfort one.
esp_err_t display_set_backlight(uint8_t percent);

// Drives the backlight pin as a plain push-pull GPIO, on/off, `cycles` times.
// Diagnostic only: it removes LEDC from the picture so a dark panel means the
// pin is not reaching the board, rather than the PWM being misconfigured.
// Call before display_init().
void display_backlight_selftest(int cycles);

// Packs RGB565 *byte-swapped* for the panel. The ILI9341 takes pixels MSB-first
// while the ESP32 is little-endian, so a raw uint16_t would arrive with its
// bytes reversed — the classic "colours are nearly right but wrong" bug.
static inline uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return __builtin_bswap16(c);
}

// Fills a rectangle in the framebuffer. Clipped to the panel.
void display_fill_rect(int x, int y, int w, int h, uint16_t colour);

// Fills the whole framebuffer.
void display_fill(uint16_t colour);
