// XPT2046 resistive touch, with calibration.
//
// Deliberately on its own SPI bus rather than shared with the TFT. Sharing saves
// three GPIOs and is the right call for the soldered build; on jumper wires it
// buys nothing and costs a junction. It is also cleaner electrically: the
// XPT2046 tops out near 2.5MHz while the panel runs at 40MHz, so a shared bus
// renegotiates clock speed on every transaction.
//
// The driver is configured to hand back raw ADC counts rather than its own
// screen coordinates. Its conversion assumes the panel swings the full 0-4096,
// which no real resistive panel does, so coordinates come out squashed and
// offset. We fit against the panel's actual range instead.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Raw ADC readings at two known screen positions per axis. A resistive panel is
// linear enough that two points per axis is plenty; the drift AGENTS.md warns
// about is slow, and recalibration is cheap.
typedef struct {
    bool swap_xy;                  // panel axes transposed relative to the display
    int32_t x_raw_lo, x_raw_hi;    // raw values at TOUCH_CAL_X_LO / _X_HI
    int32_t y_raw_lo, y_raw_hi;
} touch_cal_t;

// Calibration target positions, inset from the edges so the fit is not
// extrapolated from the panel's least linear region.
#define TOUCH_CAL_X_LO 32
#define TOUCH_CAL_X_HI 288
#define TOUCH_CAL_Y_LO 24
#define TOUCH_CAL_Y_HI 216

esp_err_t touch_init(void);

// True when a press is detected. x/y are display coordinates if calibration is
// loaded, raw ADC counts otherwise. raw_x/raw_y are always the raw counts.
// Any output may be NULL.
bool touch_read(int *x, int *y, uint16_t *raw_x, uint16_t *raw_y);

// Runs the on-screen calibration: three targets, then a linear fit. Draws to the
// display, so call after display_init(). Stores the result in NVS on success.
esp_err_t touch_calibrate(void);

// Loads calibration from NVS. ESP_ERR_NVS_NOT_FOUND when the device has never
// been calibrated.
esp_err_t touch_load_calibration(void);

bool touch_is_calibrated(void);
