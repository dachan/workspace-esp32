#include "gfx.h"

#include <math.h>

#include "canvas.h"

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

    // Static scratch, not stack. These buffers are a kilobyte apiece and the
    // ESP-IDF main task gets 3.5KB by default — nesting a few of them overflows
    // it and reboots the board. All drawing happens on one task, so a shared
    // static is safe; if that ever stops being true this needs revisiting.
    static float xs[GFX_MAX_POLY_PTS];

    for (int y = y0; y <= y1; y++) {
        float yc = (float)y + 0.5f;
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

    // Offset every vertex along the average of its two adjoining edge normals.
    //
    // The obvious shortcut — pushing points radially away from the centroid —
    // only holds for convex shapes. On something concave like the scalloped
    // mouth it flings the outermost teeth into spikes, because "away from the
    // centre" stops meaning "outward" the moment the boundary turns back on
    // itself.
    float area = 0.0f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        area += pts[j].x * pts[i].y - pts[i].x * pts[j].y;
    }
    const float sign = (area > 0.0f) ? 1.0f : -1.0f;

    static gfx_pt_t big[GFX_MAX_POLY_PTS];   // static: see gfx_fill_poly
    for (int i = 0; i < n; i++) {
        int prev = (i + n - 1) % n, next = (i + 1) % n;

        float e1x = pts[i].x - pts[prev].x, e1y = pts[i].y - pts[prev].y;
        float e2x = pts[next].x - pts[i].x, e2y = pts[next].y - pts[i].y;
        float l1 = sqrtf(e1x * e1x + e1y * e1y);
        float l2 = sqrtf(e2x * e2x + e2y * e2y);
        if (l1 < 0.0001f) { l1 = 1.0f; }
        if (l2 < 0.0001f) { l2 = 1.0f; }

        float n1x = e1y / l1 * sign, n1y = -e1x / l1 * sign;
        float n2x = e2y / l2 * sign, n2y = -e2x / l2 * sign;

        float nx = n1x + n2x, ny = n1y + n2y;
        float nl = sqrtf(nx * nx + ny * ny);
        if (nl < 0.0001f) {
            nx = n1x; ny = n1y;
        } else {
            nx /= nl; ny /= nl;
        }

        // Miter: a sharp corner needs to travel further than px to keep the
        // stroke an even thickness. Capped so near-reversals do not shoot off.
        float cosang = n1x * n2x + n1y * n2y;
        float denom = (1.0f + cosang > 0.25f) ? (1.0f + cosang) : 0.25f;
        float miter = sqrtf(2.0f / denom);
        // No mitre extension at all. Its purpose is to keep the outline an even
        // thickness around a corner, but the mouth ends in a cusp where its
        // upper and lower edges meet, and any extension there becomes a thin
        // spike shooting out across the face. Corners come out marginally
        // thinner instead, which is invisible; the spikes were not.
        if (miter > 1.0f) { miter = 1.0f; }

        big[i].x = pts[i].x + nx * px * miter;
        big[i].y = pts[i].y + ny * px * miter;
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
                    float lean, float wobble, float phase, float taper)
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
        // sin(t) > 0 is the lower half, so a positive taper widens the base and
        // narrows the crown into a dome.
        float wide = 1.0f + taper * sinf(t);
        float x = cosf(t) * rx * r * wide;
        float y = sinf(t) * ry * r;
        // Shear grows toward the top of the shape, so the body leans rather than
        // slides.
        out[i].x = cx + x + lean * (-y / ry);
        out[i].y = cy + y;
    }
    return n;
}
