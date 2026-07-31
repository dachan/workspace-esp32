#include "gfx.h"

#include <math.h>

#include "display.h"

static uint32_t s_rng = 0x1234abcdu;

uint32_t gfx_rand(void)
{
    // xorshift32 — a handful of instructions, and animation jitter does not care
    // about statistical quality.
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

float gfx_randf(void)
{
    return (float)(gfx_rand() & 0xFFFFFF) / (float)0x1000000;
}

static inline void span(int y, int xa, int xb, uint16_t colour)
{
    if (y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }
    if (xa < 0) { xa = 0; }
    if (xb > DISPLAY_WIDTH - 1) { xb = DISPLAY_WIDTH - 1; }
    if (xa > xb) {
        return;
    }
    uint16_t *row = display_framebuffer() + (size_t)y * DISPLAY_WIDTH;
    for (int x = xa; x <= xb; x++) {
        row[x] = colour;
    }
}

void gfx_fill_poly(const gfx_pt_t *pts, int n, uint16_t colour)
{
    if (n < 3 || display_framebuffer() == NULL) {
        return;
    }

    float miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].y < miny) { miny = pts[i].y; }
        if (pts[i].y > maxy) { maxy = pts[i].y; }
    }

    int y0 = (int)floorf(miny);
    int y1 = (int)ceilf(maxy);
    if (y0 < 0) { y0 = 0; }
    if (y1 > DISPLAY_HEIGHT - 1) { y1 = DISPLAY_HEIGHT - 1; }

    for (int y = y0; y <= y1; y++) {
        float yc = (float)y + 0.5f;
        float xs[GFX_MAX_POLY_PTS];
        int cnt = 0;

        for (int i = 0, j = n - 1; i < n; j = i++) {
            float ya = pts[i].y, yb = pts[j].y;
            // Half-open test: a vertex exactly on the scanline counts once, so
            // shared edges do not double-toggle and leave holes.
            if ((ya <= yc && yb > yc) || (yb <= yc && ya > yc)) {
                float t = (yc - ya) / (yb - ya);
                if (cnt < GFX_MAX_POLY_PTS) {
                    xs[cnt++] = pts[i].x + t * (pts[j].x - pts[i].x);
                }
            }
        }
        if (cnt < 2) {
            continue;
        }

        // Insertion sort: cnt is tiny (2-8 for these shapes).
        for (int i = 1; i < cnt; i++) {
            float v = xs[i];
            int k = i - 1;
            while (k >= 0 && xs[k] > v) {
                xs[k + 1] = xs[k];
                k--;
            }
            xs[k + 1] = v;
        }

        for (int i = 0; i + 1 < cnt; i += 2) {
            span(y, (int)ceilf(xs[i] - 0.5f), (int)floorf(xs[i + 1] - 0.5f), colour);
        }
    }
}

void gfx_fill_poly_outlined(const gfx_pt_t *pts, int n, uint16_t fill,
                            uint16_t edge, float px)
{
    if (n < 3 || n > GFX_MAX_POLY_PTS) {
        return;
    }

    float cx = 0, cy = 0;
    for (int i = 0; i < n; i++) {
        cx += pts[i].x;
        cy += pts[i].y;
    }
    cx /= (float)n;
    cy /= (float)n;

    // Push each point outward along its own radius from the centroid. Exact only
    // for convex shapes, which is all the creature uses.
    gfx_pt_t big[GFX_MAX_POLY_PTS];
    for (int i = 0; i < n; i++) {
        float dx = pts[i].x - cx, dy = pts[i].y - cy;
        float d = sqrtf(dx * dx + dy * dy);
        float k = (d > 0.001f) ? (d + px) / d : 1.0f;
        big[i].x = cx + dx * k;
        big[i].y = cy + dy * k;
    }

    gfx_fill_poly(big, n, edge);
    gfx_fill_poly(pts, n, fill);
}

void gfx_fill_ellipse(float cx, float cy, float rx, float ry, uint16_t colour)
{
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    int y0 = (int)floorf(cy - ry);
    int y1 = (int)ceilf(cy + ry);
    if (y0 < 0) { y0 = 0; }
    if (y1 > DISPLAY_HEIGHT - 1) { y1 = DISPLAY_HEIGHT - 1; }

    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry;
        float s = 1.0f - dy * dy;
        if (s <= 0) {
            continue;
        }
        float half = rx * sqrtf(s);
        span(y, (int)ceilf(cx - half), (int)floorf(cx + half), colour);
    }
}

int gfx_blob_points(gfx_pt_t *out, int n, float cx, float cy, float rx, float ry,
                    float lean, float wobble, float phase)
{
    if (n > GFX_MAX_POLY_PTS) {
        n = GFX_MAX_POLY_PTS;
    }
    for (int i = 0; i < n; i++) {
        float t = 6.2831853f * (float)i / (float)n;
        // Two out-of-phase harmonics: enough to look organic, cheap enough to
        // run per frame, and never repeating exactly because phase drifts.
        float r = 1.0f + wobble * (sinf(3.0f * t + phase) * 0.6f
                                   + sinf(2.0f * t - phase * 0.7f) * 0.4f);
        float x = cosf(t) * rx * r;
        float y = sinf(t) * ry * r;
        // Shear grows toward the top of the shape, so the body leans rather than
        // slides.
        out[i].x = cx + x + lean * (-y / ry);
        out[i].y = cy + y;
    }
    return n;
}
