#include "creature.h"

#include <math.h>

#include "display.h"
#include "gfx.h"

#define PI 3.14159265f

// Palette lifted from the reference art: flat fills, one darker tone for the
// underside, heavy near-black outline.
static uint16_t C_BG, C_BODY, C_SHADE, C_MOUTH, C_TONGUE, C_EDGE;

// Where the creature sits. Everything else is expressed relative to this.
#define BODY_CX 160.0f
#define BODY_CY 148.0f
#define BODY_RX 78.0f
#define BODY_RY 80.0f

typedef struct {
    // Continuous emotion. Everything visible is derived from these two.
    float valence;   // 0 distressed .. 1 delighted
    float arousal;   // 0 calm .. 1 excited

    // Rendering parameters, all smoothed toward targets rather than snapped.
    float breathe;      // phase
    float wobble_phase;
    float lean;
    float squash;       // >0 squat and wide, <0 stretched tall
    float mouth_open;
    float smile;
    float eye_open;
    float gaze_x, gaze_y;

    // Blink and saccade timers. Both intervals are randomised every time: a
    // fixed-period blink reads as a machine immediately.
    float blink_timer, blink_t;
    float saccade_timer;
    float gaze_tx, gaze_ty;

    bool was_touched;
} creature_t;

static creature_t c;

static inline float approach(float v, float target, float rate, float dt)
{
    float k = rate * dt;
    if (k > 1.0f) { k = 1.0f; }
    return v + (target - v) * k;
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void creature_init(void)
{
    C_BG     = display_rgb(246, 244, 248);
    C_BODY   = display_rgb(186, 108, 190);
    C_SHADE  = display_rgb(148, 76, 154);
    C_MOUTH  = display_rgb(74, 38, 82);
    C_TONGUE = display_rgb(205, 145, 200);
    C_EDGE   = display_rgb(24, 14, 28);

    c.valence = 0.6f;
    c.arousal = 0.25f;
    c.eye_open = 1.0f;
    c.mouth_open = 0.35f;
    c.smile = 0.6f;
    c.blink_timer = 2.0f;
    c.saccade_timer = 1.0f;
}

void creature_update(float dt, bool touched, int touch_x, int touch_y)
{
    c.breathe += dt * 1.1f;
    c.wobble_phase += dt * 0.55f;

    // --- reflex layer: immediate, no decisions ---------------------------
    // Being poked is a jolt first and a feeling second. The squash and the gaze
    // snap happen on the same frame as the touch; the mood catches up after.
    if (touched) {
        float tx = clampf((float)touch_x, 0, DISPLAY_WIDTH);
        float ty = clampf((float)touch_y, 0, DISPLAY_HEIGHT);
        c.gaze_tx = clampf((tx - BODY_CX) / BODY_RX, -1.0f, 1.0f);
        c.gaze_ty = clampf((ty - BODY_CY) / BODY_RY, -1.0f, 1.0f);
        c.saccade_timer = 0.6f;

        if (!c.was_touched) {
            c.squash += 0.5f;              // impulse on the leading edge only
            c.arousal = clampf(c.arousal + 0.45f, 0, 1);
            c.valence = clampf(c.valence + 0.12f, 0, 1);
        }
        c.arousal = clampf(c.arousal + dt * 0.35f, 0, 1);
        c.lean = approach(c.lean, c.gaze_tx * 9.0f, 8.0f, dt);
    } else {
        c.lean = approach(c.lean, sinf(c.breathe * 0.31f) * 3.0f, 1.5f, dt);
    }
    c.was_touched = touched;

    // --- cognition layer: slow drift back to baseline --------------------
    c.arousal = approach(c.arousal, 0.22f, 0.35f, dt);
    c.valence = approach(c.valence, 0.55f, 0.12f, dt);
    c.squash = approach(c.squash, 0.0f, 6.0f, dt);

    // --- emotion drives the visible parameters ---------------------------
    float mouth_target = 0.20f + c.arousal * 0.55f + c.valence * 0.12f;
    float smile_target = 0.15f + c.valence * 0.85f;
    c.mouth_open = approach(c.mouth_open, mouth_target, 7.0f, dt);
    c.smile = approach(c.smile, smile_target, 4.0f, dt);

    // --- blinking --------------------------------------------------------
    c.blink_timer -= dt;
    if (c.blink_timer <= 0.0f) {
        c.blink_t = 1.0f;
        // Randomised interval, shorter when alert. Never a fixed period.
        c.blink_timer = 1.4f + gfx_randf() * 4.0f - c.arousal * 0.8f;
    }
    if (c.blink_t > 0.0f) {
        c.blink_t -= dt * 9.0f;
        if (c.blink_t < 0.0f) { c.blink_t = 0.0f; }
    }
    // Triangular profile: shuts fast, opens fast, no dwell.
    float lid = 1.0f - fabsf(c.blink_t * 2.0f - 1.0f);
    c.eye_open = clampf(1.0f - lid * 1.15f, 0.05f, 1.0f);

    // --- saccades --------------------------------------------------------
    // A perfectly steady eye reads as dead, so the gaze never fully settles.
    c.saccade_timer -= dt;
    if (c.saccade_timer <= 0.0f) {
        c.gaze_tx = (gfx_randf() - 0.5f) * 1.1f;
        c.gaze_ty = (gfx_randf() - 0.5f) * 0.7f;
        c.saccade_timer = 0.5f + gfx_randf() * 2.2f;
    }
    c.gaze_x = approach(c.gaze_x, c.gaze_tx, 14.0f, dt);
    c.gaze_y = approach(c.gaze_y, c.gaze_ty, 14.0f, dt);
}

// A tapered blade: out along the top edge to the tip, back along the bottom.
// Used for the ear-fin and the stubby limbs.
static void draw_blade(float bx, float by, float tipx, float tipy, float w)
{
    float dx = tipx - bx, dy = tipy - by;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        return;
    }
    float nx = -dy / len, ny = dx / len;   // unit normal

    gfx_pt_t p[6];
    p[0] = (gfx_pt_t){bx + nx * w, by + ny * w};
    p[1] = (gfx_pt_t){bx + dx * 0.45f + nx * w * 0.75f, by + dy * 0.45f + ny * w * 0.75f};
    p[2] = (gfx_pt_t){tipx, tipy};
    p[3] = (gfx_pt_t){bx + dx * 0.45f - nx * w * 0.62f, by + dy * 0.45f - ny * w * 0.62f};
    p[4] = (gfx_pt_t){bx - nx * w, by - ny * w};
    p[5] = (gfx_pt_t){bx, by + w * 0.4f};
    gfx_fill_poly_outlined(p, 6, C_BODY, C_EDGE, 3.0f);
}

static void draw_mouth(void)
{
    const int teeth = 9;                       // zigzag segments per edge
    const float half_w = 56.0f + c.smile * 4.0f;
    const float corner_y = 126.0f - c.smile * 9.0f;
    const float sag_top = 8.0f + c.smile * 5.0f;
    const float depth = 18.0f + c.mouth_open * 46.0f;
    const float tooth = 8.0f;

    gfx_pt_t p[GFX_MAX_POLY_PTS];
    int n = 0;

    float x0 = BODY_CX - half_w + c.lean * 0.5f;
    float x1 = BODY_CX + half_w + c.lean * 0.5f;

    // Upper edge, left to right, teeth pointing down into the mouth.
    for (int i = 0; i <= teeth; i++) {
        float u = (float)i / (float)teeth;
        float x = x0 + (x1 - x0) * u;
        float base = corner_y + (126.0f + sag_top - corner_y) * sinf(PI * u);
        p[n++] = (gfx_pt_t){x, base + ((i & 1) ? tooth : 0.0f)};
    }
    // Lower edge, right back to left, teeth pointing up.
    for (int i = teeth; i >= 0; i--) {
        float u = (float)i / (float)teeth;
        float x = x0 + (x1 - x0) * u;
        float base = corner_y + (126.0f + depth - corner_y) * sinf(PI * u);
        p[n++] = (gfx_pt_t){x, base - ((i & 1) ? tooth : 0.0f)};
    }

    gfx_fill_poly_outlined(p, n, C_MOUTH, C_EDGE, 3.0f);

    // Tongue, only once the mouth is open enough to see into.
    if (c.mouth_open > 0.32f) {
        float t = (c.mouth_open - 0.32f) / 0.68f;
        gfx_fill_ellipse(BODY_CX + c.lean * 0.5f - 6.0f,
                         126.0f + depth * 0.62f,
                         26.0f * t + 10.0f, 15.0f * t + 5.0f, C_TONGUE);
    }
}

void creature_draw(void)
{
    display_fill(C_BG);

    // Breathing scales the body very slightly; squash trades height for width so
    // volume looks conserved when it reacts.
    float breath = sinf(c.breathe) * 0.022f;
    float rx = BODY_RX * (1.0f + breath * 0.5f + c.squash * 0.10f);
    float ry = BODY_RY * (1.0f + breath - c.squash * 0.13f);
    float cy = BODY_CY + c.squash * 8.0f;

    // Limbs and fin sit behind the body so their bases are hidden by it.
    draw_blade(BODY_CX - 62.0f + c.lean * 0.6f, cy - 34.0f,
               BODY_CX - 108.0f + c.lean * 1.4f, cy - 88.0f, 13.0f);
    draw_blade(BODY_CX - 40.0f + c.lean * 0.3f, cy + ry * 0.80f,
               BODY_CX - 58.0f, cy + ry * 1.06f, 12.0f);
    draw_blade(BODY_CX + 40.0f + c.lean * 0.3f, cy + ry * 0.80f,
               BODY_CX + 60.0f, cy + ry * 1.04f, 12.0f);

    // Body: fill the whole silhouette in the shade tone, then lay a slightly
    // smaller, offset copy of the light tone on top. The crescent left over is
    // the underside shadow — two fills instead of any clipping.
    gfx_pt_t body[56];
    int n = gfx_blob_points(body, 56, BODY_CX, cy, rx, ry,
                            c.lean, 0.018f, c.wobble_phase);
    gfx_fill_poly_outlined(body, n, C_SHADE, C_EDGE, 3.0f);

    n = gfx_blob_points(body, 56, BODY_CX + 3.0f, cy - 6.0f,
                        rx * 0.945f, ry * 0.945f, c.lean, 0.018f, c.wobble_phase);
    gfx_fill_poly(body, n, C_BODY);

    // Eyes: small dark ovals. Blink collapses height, so no separate lid shape.
    float ex = 27.0f, ey = cy - ry * 0.52f;
    float gx = c.gaze_x * 4.5f + c.lean * 0.6f, gy = c.gaze_y * 3.0f;
    gfx_fill_ellipse(BODY_CX - ex + gx, ey + gy, 6.5f, 9.0f * c.eye_open, C_EDGE);
    gfx_fill_ellipse(BODY_CX + ex + gx, ey + gy, 6.5f, 9.0f * c.eye_open, C_EDGE);

    draw_mouth();
}
