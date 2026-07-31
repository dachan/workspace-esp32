#include "creature.h"

#include <math.h>

#include "canvas.h"
#include "gfx.h"

#define PI 3.14159265f

// Palette lifted from the reference art: flat fills, one darker tone for the
// underside, heavy near-black outline.
static uint16_t C_BG, C_BODY, C_SHADE, C_MOUTH, C_TONGUE, C_EDGE;

// Where the creature sits. Everything else is expressed relative to this.
#define BODY_CX 160.0f
#define BODY_CY 140.0f
#define BODY_RX 82.0f
#define BODY_RY 92.0f

// Widens the base and narrows the crown, turning the circle into the egg-shaped
// dome of the reference art.
#define BODY_TAPER 0.16f

// Thickness of the underside shadow, in pixels. The shadow is the lower arc of
// the body paired with the same arc pulled inward, so its edge curves along the
// body rather than cutting across it at an angle.
#define SHADOW_DEPTH 20.0f

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

// A tapered fin. The spine is a quadratic bezier so it curves rather than
// running straight, the width falls off as a cosine so the edges never kink, and
// the tip is capped with a small arc — nothing on the creature is a hard corner.
#define BLADE_STEPS 12

static void draw_blade(float bx, float by, float tipx, float tipy, float w, float bend)
{
    const int steps = BLADE_STEPS;
    const float tip_u = 0.88f;      // stop short of the point and cap it

    float dx = tipx - bx, dy = tipy - by;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        return;
    }
    // Bezier control point, pushed off the chord to curve the fin.
    float ctlx = bx + dx * 0.5f - dy / len * bend;
    float ctly = by + dy * 0.5f + dx / len * bend;

    static gfx_pt_t p[GFX_MAX_POLY_PTS];
    int n = 0;

    float sx[BLADE_STEPS + 1], sy[BLADE_STEPS + 1], sw[BLADE_STEPS + 1];
    for (int i = 0; i <= steps; i++) {
        float u = tip_u * (float)i / (float)steps;
        float m = 1.0f - u;
        sx[i] = m * m * bx + 2.0f * m * u * ctlx + u * u * tipx;
        sy[i] = m * m * by + 2.0f * m * u * ctly + u * u * tipy;
        sw[i] = w * cosf(u * PI * 0.5f);
    }

    // Out along one side...
    for (int i = 0; i <= steps; i++) {
        int j = (i == steps) ? i - 1 : i;
        float tx = sx[j + 1 > steps ? steps : j + 1] - sx[j];
        float ty = sy[j + 1 > steps ? steps : j + 1] - sy[j];
        float tl = sqrtf(tx * tx + ty * ty);
        if (tl < 0.001f) { tl = 1.0f; }
        p[n++] = (gfx_pt_t){sx[i] - ty / tl * sw[i], sy[i] + tx / tl * sw[i]};
    }
    // ...round the tip...
    {
        float tx = tipx - sx[steps], ty = tipy - sy[steps];
        float tl = sqrtf(tx * tx + ty * ty);
        if (tl < 0.001f) { tl = 1.0f; }
        float ang0 = atan2f(tx / tl, -ty / tl);
        for (int i = 1; i < 5; i++) {
            float a = ang0 - PI * (float)i / 5.0f;
            p[n++] = (gfx_pt_t){sx[steps] + sinf(a) * sw[steps],
                                sy[steps] - cosf(a) * sw[steps]};
        }
    }
    // ...and back along the other.
    for (int i = steps; i >= 0; i--) {
        int j = (i == steps) ? i - 1 : i;
        float tx = sx[j + 1 > steps ? steps : j + 1] - sx[j];
        float ty = sy[j + 1 > steps ? steps : j + 1] - sy[j];
        float tl = sqrtf(tx * tx + ty * ty);
        if (tl < 0.001f) { tl = 1.0f; }
        p[n++] = (gfx_pt_t){sx[i] + ty / tl * sw[i], sy[i] - tx / tl * sw[i]};
    }

    gfx_fill_poly_outlined(p, n, C_BODY, C_EDGE, 3.0f);
}

static void draw_mouth(void)
{
    // Few, large teeth. Many small ones read as noise at this resolution rather
    // than as a grin — the reference has about five to a side.
    const int teeth = 5;
    const float half_w = 66.0f + c.smile * 6.0f;
    const float corner_y = 124.0f - c.smile * 10.0f;
    const float sag_top = 10.0f + c.smile * 6.0f;
    // Kept comfortably deeper than twice the tooth height so the upper and lower
    // scallops can never meet and tangle the outline.
    const float depth = 42.0f + c.mouth_open * 46.0f;
    const float tooth = 13.0f;

    static gfx_pt_t p[GFX_MAX_POLY_PTS];
    int n = 0;

    float x0 = BODY_CX - half_w + c.lean * 0.5f;
    float x1 = BODY_CX + half_w + c.lean * 0.5f;

    // Teeth are a raised cosine rather than a zigzag: same toothy read, but the
    // tips and valleys are rounded instead of being sharp vertices. Sampled
    // several times per tooth so the curve stays smooth on screen.
    const int per_tooth = 5;
    const int steps = teeth * per_tooth;

    // Upper edge, left to right, teeth hanging down into the mouth.
    for (int i = 0; i <= steps; i++) {
        float u = (float)i / (float)steps;
        float x = x0 + (x1 - x0) * u;
        float base = corner_y + (126.0f + sag_top - corner_y) * sinf(PI * u);
        float bump = 0.5f - 0.5f * cosf(u * (float)teeth * 2.0f * PI);
        p[n++] = (gfx_pt_t){x, base + tooth * bump};
    }
    // Lower edge, right back to left, teeth rising from the jaw.
    for (int i = steps; i >= 0; i--) {
        float u = (float)i / (float)steps;
        float x = x0 + (x1 - x0) * u;
        float base = corner_y + (126.0f + depth - corner_y) * sinf(PI * u);
        float bump = 0.5f - 0.5f * cosf(u * (float)teeth * 2.0f * PI);
        p[n++] = (gfx_pt_t){x, base - tooth * bump};
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
    draw_blade(BODY_CX - 58.0f + c.lean * 0.6f, cy - 30.0f,
               BODY_CX - 96.0f + c.lean * 1.4f, cy - 74.0f, 19.0f, 7.0f);
    draw_blade(BODY_CX - 36.0f + c.lean * 0.3f, cy + ry * 0.74f,
               BODY_CX - 62.0f, cy + ry * 1.00f, 17.0f, 7.0f);
    draw_blade(BODY_CX + 36.0f + c.lean * 0.3f, cy + ry * 0.74f,
               BODY_CX + 64.0f, cy + ry * 0.98f, 17.0f, -7.0f);

    const int BODY_PTS = 56;
    static gfx_pt_t body[GFX_MAX_POLY_PTS];
    int n = gfx_blob_points(body, BODY_PTS, BODY_CX, cy, rx, ry,
                            c.lean, 0.018f, c.wobble_phase, BODY_TAPER);
    gfx_fill_poly_outlined(body, n, C_BODY, C_EDGE, 3.0f);

    // Underside shadow: the body's lower arc, closed off by the same arc pulled
    // inward toward the centre. Both edges follow the silhouette, so the shadow
    // curves along the body and stays symmetric — an offset second blob would
    // instead skew the boundary in whatever direction it was offset.
    {
        const int half = BODY_PTS / 2;   // t in [0, PI) traces the lower half
        static gfx_pt_t sh[GFX_MAX_POLY_PTS];
        int m = 0;
        for (int i = 0; i <= half; i++) {
            sh[m++] = body[i];
        }
        for (int i = half; i >= 0; i--) {
            // Depth fades to nothing at the two ends, so the crescent tapers to
            // points instead of being cut off with a blunt vertical edge.
            float u = (float)i / (float)half;
            float depth = SHADOW_DEPTH * sinf(PI * u);
            float dx = body[i].x - BODY_CX, dy = body[i].y - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float k = (d > 0.001f) ? (d - depth) / d : 0.0f;
            if (k < 0.0f) { k = 0.0f; }
            sh[m++] = (gfx_pt_t){BODY_CX + dx * k, cy + dy * k};
        }
        gfx_fill_poly(sh, m, C_SHADE);
    }

    // Eyes: small dark ovals. Blink collapses height, so no separate lid shape.
    float ex = 27.0f, ey = cy - ry * 0.52f;
    float gx = c.gaze_x * 4.5f + c.lean * 0.6f, gy = c.gaze_y * 3.0f;
    gfx_fill_ellipse(BODY_CX - ex + gx, ey + gy, 6.5f, 9.0f * c.eye_open, C_EDGE);
    gfx_fill_ellipse(BODY_CX + ex + gx, ey + gy, 6.5f, 9.0f * c.eye_open, C_EDGE);

    draw_mouth();
}
