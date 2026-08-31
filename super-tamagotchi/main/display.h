// Display bring-up: ILI9341 over SPI, with the framebuffer living in PSRAM.
//
// The PSRAM framebuffer is not incidental — it is the rendering architecture the
// pet needs. 320x240x16bpp is 150KB, trivial against 8MB, so the creature can be
// composed off-screen and blitted whole rather than drawn in fiddly dirty rects.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canvas.h"
#include "esp_err.h"

// Brings up SPI, the panel, and the backlight, and allocates the PSRAM
// framebuffer. Safe to call once.
esp_err_t display_init(void);

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

