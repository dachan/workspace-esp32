// Minimal polygon/ellipse rasteriser over the PSRAM framebuffer.
//
// Deliberately not a sprite blitter. The creature is drawn from parameters every
// frame rather than played back from stored frames, which is what makes "never
// repeat a motion exactly" free instead of something to engineer around. Filling
// a few dozen primitives at 240MHz is nothing next to the SPI flush.

#pragma once

#include <stdint.h>

#define GFX_MAX_POLY_PTS 128

typedef struct {
    float x, y;
} gfx_pt_t;

// Scanline-fills a closed polygon. Points may wind either way.
void gfx_fill_poly(const gfx_pt_t *pts, int n, uint16_t colour);

// Fills the polygon with `fill`, over a copy expanded by `px` pixels in `edge`.
// Cheapest way to get the reference art's heavy black outline: draw a slightly
// larger silhouette behind the shape rather than stroking the boundary.
void gfx_fill_poly_outlined(const gfx_pt_t *pts, int n, uint16_t fill,
                            uint16_t edge, float px);

void gfx_fill_ellipse(float cx, float cy, float rx, float ry, uint16_t colour);

// Generates `n` points around an ellipse, with a horizontal shear that grows
// toward the top (`lean`), a low-frequency radial wobble (`wobble`, animated by
// `phase`) so the silhouette is never perfectly still, and `taper` to narrow the
// top relative to the bottom — an egg rather than a circle.
int gfx_blob_points(gfx_pt_t *out, int n, float cx, float cy, float rx, float ry,
                    float lean, float wobble, float phase, float taper);

// Fast, cheap PRNG — animation jitter needs volume, not cryptographic quality.
uint32_t gfx_rand(void);
float gfx_randf(void);
