#pragma once

#include <stdint.h>

enum {
    COLOR_PALETTE_COUNT = 8,
    // Four primary hues, each immediately followed by its lighter companion tint.
    COLOR_PALETTE_COLOUR_COUNT = 8,
};

typedef struct {
    const char *name;
    uint8_t colour[COLOR_PALETTE_COLOUR_COUNT][3];
} color_palette_t;

extern const color_palette_t COLOR_PALETTES[COLOR_PALETTE_COUNT];

void color_palette_sample(uint8_t palette_index, uint8_t position, uint8_t brightness,
                          uint8_t *red, uint8_t *green, uint8_t *blue);
