// XPT2046 resistive touch.
//
// Deliberately on its own SPI bus rather than shared with the TFT. Sharing saves
// three GPIOs and is the right call for the soldered build; on jumper wires it
// buys nothing and costs a junction. It is also cleaner electrically: the
// XPT2046 tops out near 2.5MHz while the panel runs at 40MHz, so a shared bus
// renegotiates clock speed on every transaction.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t touch_init(void);

// Reads one sample. Returns true when the panel is being pressed.
// x/y are in display coordinates; raw_x/raw_y are the controller's own values,
// which is what calibration has to be derived from. Any of the outputs may be
// NULL.
bool touch_read(int *x, int *y, uint16_t *raw_x, uint16_t *raw_y);
