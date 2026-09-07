#pragma once

#include <stdint.h>

/* Header brand artwork, generated into logo.c by scripts/generate_logos.py.
 * alpha is a row-major coverage mask with no row padding; baseline is the row
 * offset of the lettering's baseline so both logos align to the panel text. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t baseline;
    const uint8_t *alpha;
} logo_t;

extern const logo_t logo_cursor;
extern const logo_t logo_openai;
