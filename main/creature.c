#include "creature.h"

#include <math.h>

#include "canvas.h"
#include "gfx.h"

#define PI 3.14159265f

// Palette lifted from the reference art: flat fills, one darker tone for the
// underside, heavy near-black outline.
static uint16_t C_BG, C_BODY, C_SHADE, C_MOUTH, C_TONGUE, C_EDGE, C_PUPIL;

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

    // Cheap pseudo-depth. `facing` sweeps the fin horizontally: at 0 it is
    // directly behind the body and hidden, which reads as facing the viewer, and
    // it emerges on whichever side the creature has turned away from. `depth`
    // is 0 far / 1 near and drives one overall scale.
    float facing, facing_target;
    float depth, depth_target;
    float turn_timer, depth_timer;

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
    C_PUPIL  = display_rgb(255, 255, 255);

    c.valence = 0.6f;
    c.arousal = 0.25f;
    c.eye_open = 1.0f;
    c.mouth_open = 0.35f;
    c.smile = 0.6f;
    c.blink_timer = 2.0f;
    c.saccade_timer = 1.0f;
    c.facing = c.facing_target = 1.0f;
    c.depth = c.depth_target = 0.85f;
    c.turn_timer = 3.0f;
    c.depth_timer = 4.0f;
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

        // Turn away from the poke and lean in toward the viewer.
        c.facing_target = (c.gaze_tx > 0.0f) ? -1.0f : 1.0f;
        c.depth_target = 1.0f;
        c.turn_timer = 2.5f;
        c.depth_timer = 3.0f;
    } else {
        c.lean = approach(c.lean, sinf(c.breathe * 0.31f) * 3.0f, 1.5f, dt);
    }
    c.was_touched = touched;

    // --- idle turning and drifting nearer / further ----------------------
    // Both intervals randomised, like the blink: anything on a fixed period
    // starts to read as a mechanism rather than a creature.
    c.turn_timer -= dt;
    if (c.turn_timer <= 0.0f) {
        c.facing_target = (gfx_randf() < 0.5f) ? -1.0f : 1.0f;
        c.turn_timer = 2.5f + gfx_randf() * 6.0f - c.arousal * 1.5f;
    }
    c.depth_timer -= dt;
    if (c.depth_timer <= 0.0f) {
        c.depth_target = 0.18f + gfx_randf() * 0.82f;
        c.depth_timer = 3.0f + gfx_randf() * 7.0f;
    }
    c.facing = approach(c.facing, c.facing_target, 2.2f + c.arousal * 2.0f, dt);
    c.depth = approach(c.depth, c.depth_target, 0.8f, dt);

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

    // Unit tangent at each spine sample, from its neighbours.
    float tanx[BLADE_STEPS + 1], tany[BLADE_STEPS + 1];
    for (int i = 0; i <= steps; i++) {
        int a = (i > 0) ? i - 1 : i;
        int b = (i < steps) ? i + 1 : i;
        float ux = sx[b] - sx[a], uy = sy[b] - sy[a];
        float ul = sqrtf(ux * ux + uy * uy);
        if (ul < 0.0001f) { ul = 1.0f; }
        tanx[i] = ux / ul;
        tany[i] = uy / ul;
    }

    // Out along the left side...
    for (int i = 0; i <= steps; i++) {
        p[n++] = (gfx_pt_t){sx[i] - tany[i] * sw[i], sy[i] + tanx[i] * sw[i]};
    }
    // ...semicircle around the tip...
    //
    // Swept as nL*cos(s*PI) + tangent*sin(s*PI): at s=0 that is the left normal,
    // at s=0.5 straight ahead, at s=1 the right normal. Building it from the
    // vectors themselves rather than from an atan2 angle keeps the winding
    // consistent with the two sides — get that wrong and the polygon
    // self-intersects, which the even-odd fill leaves as a hole showing the
    // background through the fin.
    {
        const int cap = 7;
        float nlx = -tany[steps], nly = tanx[steps];
        for (int i = 1; i < cap; i++) {
            float s = (float)i / (float)cap;
            float cs = cosf(s * PI), sn = sinf(s * PI);
            p[n++] = (gfx_pt_t){sx[steps] + (nlx * cs + tanx[steps] * sn) * sw[steps],
                                sy[steps] + (nly * cs + tany[steps] * sn) * sw[steps]};
        }
    }
    // ...and back along the right.
    for (int i = steps; i >= 0; i--) {
        p[n++] = (gfx_pt_t){sx[i] + tany[i] * sw[i], sy[i] - tanx[i] * sw[i]};
    }

    gfx_fill_poly_outlined(p, n, C_BODY, C_EDGE, 3.0f);
}

// `cx`/`cy` are the body centre and `s` the overall depth scale, so the mouth
// tracks the creature as it moves nearer and further rather than sitting at
// fixed screen coordinates.
static void draw_mouth(float cx, float cy, float s)
{
    // Few, large teeth. Many small ones read as noise at this resolution rather
    // than as a grin — the reference has about five to a side.
    const int teeth = 4;
    // Turning narrows the grin, the same foreshortening a real turn would give.
    const float turn = 1.0f - 0.16f * fabsf(c.facing);
    const float half_w = (66.0f + c.smile * 6.0f) * s * turn;
    const float m0 = cy - 14.0f * s;                  // mouth reference line
    const float corner_y = m0 - (2.0f + c.smile * 10.0f) * s;
    const float sag_top = (10.0f + c.smile * 6.0f) * s;
    // Kept comfortably deeper than twice the tooth height so the upper and lower
    // scallops can never meet and tangle the outline.
    const float depth = (42.0f + c.mouth_open * 46.0f) * s;
    const float tooth = 9.0f * s;

    static gfx_pt_t p[GFX_MAX_POLY_PTS];
    int n = 0;

    float mx = cx + (c.lean * 0.5f + c.facing * 7.0f) * s;
    float x0 = mx - half_w;
    float x1 = mx + half_w;

    // Teeth are a raised cosine rather than a zigzag: same toothy read, but the
    // tips and valleys are rounded instead of being sharp vertices. Sampled
    // several times per tooth so the curve stays smooth on screen.
    const int per_tooth = 9;
    const int steps = teeth * per_tooth;

    // The two edges are sampled just inside the ends rather than all the way to
    // them. Meeting exactly would make each corner a cusp, where the outline
    // offset has no well-defined direction and throws a thin spike across the
    // face; a few pixels of separation turns each corner into a short rounded
    // edge instead.
    const float end_inset = 0.035f;

    // Upper edge, left to right, teeth hanging down into the mouth.
    for (int i = 0; i <= steps; i++) {
        float u = end_inset + (1.0f - 2.0f * end_inset) * (float)i / (float)steps;
        float x = x0 + (x1 - x0) * u;
        float base = corner_y + (m0 + sag_top - corner_y) * sinf(PI * u);
        float raised = 0.5f - 0.5f * cosf(u * (float)teeth * 2.0f * PI);
        float bump = raised * raised;
        p[n++] = (gfx_pt_t){x, base + tooth * bump};
    }
    // Lower edge, right back to left, teeth rising from the jaw.
    for (int i = steps; i >= 0; i--) {
        float u = end_inset + (1.0f - 2.0f * end_inset) * (float)i / (float)steps;
        float x = x0 + (x1 - x0) * u;
        float base = corner_y + (m0 + depth - corner_y) * sinf(PI * u);
        float raised = 0.5f - 0.5f * cosf(u * (float)teeth * 2.0f * PI);
        float bump = raised * raised;
        p[n++] = (gfx_pt_t){x, base - tooth * bump};
    }

    gfx_fill_poly_outlined(p, n, C_MOUTH, C_EDGE, 3.0f * s);

    // Tongue, only once the mouth is open enough to see into.
    if (c.mouth_open > 0.32f) {
        float t = (c.mouth_open - 0.32f) / 0.68f;
        gfx_fill_ellipse(mx - 6.0f * s, m0 + depth * 0.62f,
                         (26.0f * t + 10.0f) * s * turn, (15.0f * t + 5.0f) * s,
                         C_TONGUE);
    }
}

void creature_draw(void)
{
    display_fill(C_BG);

    // One scale carries the whole creature, and moving away also lifts it up the
    // screen — the two cues together read as distance far more convincingly than
    // scale alone.
    const float s = 0.66f + 0.34f * c.depth;
    const float cx = BODY_CX;

    // Breathing scales the body very slightly; squash trades height for width so
    // volume looks conserved when it reacts.
    float breath = sinf(c.breathe) * 0.022f;
    float rx = BODY_RX * s * (1.0f + breath * 0.5f + c.squash * 0.10f);
    float ry = BODY_RY * s * (1.0f + breath - c.squash * 0.13f);
    float cy = BODY_CY - (1.0f - c.depth) * 30.0f + c.squash * 8.0f * s;

    // The fin sweeps across with `facing`, passing behind the body at the
    // midpoint. Thinning it as it crosses sells the swap as a turn rather than a
    // teleport, since an edge-on fin should almost vanish.
    {
        float f = c.facing;
        float w = 19.0f * s * (0.30f + 0.70f * fabsf(f));
        draw_blade(cx - f * 58.0f * s + c.lean * 0.6f, cy - 30.0f * s,
                   cx - f * 96.0f * s + c.lean * 1.4f, cy - 74.0f * s,
                   w, f * 7.0f);
    }

    // Feet shift a little with the turn so the body does not look bolted down.
    float fx = c.facing * 6.0f * s;
    draw_blade(cx - 36.0f * s + fx + c.lean * 0.3f, cy + ry * 0.74f,
               cx - 62.0f * s + fx, cy + ry * 1.00f, 17.0f * s, 7.0f);
    draw_blade(cx + 36.0f * s + fx + c.lean * 0.3f, cy + ry * 0.74f,
               cx + 64.0f * s + fx, cy + ry * 0.98f, 17.0f * s, -7.0f);

    const int BODY_PTS = 56;
    static gfx_pt_t body[GFX_MAX_POLY_PTS];
    int n = gfx_blob_points(body, BODY_PTS, cx, cy, rx, ry,
                            c.lean, 0.018f, c.wobble_phase, BODY_TAPER);
    gfx_fill_poly_outlined(body, n, C_BODY, C_EDGE, 3.0f * s);

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
            float d = SHADOW_DEPTH * s * sinf(PI * u);
            float dx = body[i].x - cx, dy = body[i].y - cy;
            float dd = sqrtf(dx * dx + dy * dy);
            float k = (dd > 0.001f) ? (dd - d) / dd : 0.0f;
            if (k < 0.0f) { k = 0.0f; }
            sh[m++] = (gfx_pt_t){cx + dx * k, cy + dy * k};
        }
        gfx_fill_poly(sh, m, C_SHADE);
    }

    // Eyes: small dark ovals. Blink collapses height, so no separate lid shape.
    // They slide with the turn, and the trailing one narrows as it goes round.
    float ey = cy - ry * 0.52f;
    float gx = (c.gaze_x * 4.5f + c.lean * 0.6f + c.facing * 9.0f) * s;
    float gy = c.gaze_y * 3.0f * s;
    for (int i = 0; i < 2; i++) {
        float side = (i == 0) ? -1.0f : 1.0f;
        float squeeze = 1.0f - 0.35f * fabsf(c.facing) * ((side * c.facing > 0) ? 1.0f : 0.0f);
        float ex = cx + side * 27.0f * s + gx;
        float erx = 6.5f * s * squeeze, ery = 9.0f * s * c.eye_open;
        gfx_fill_ellipse(ex, ey + gy, erx, ery, C_EDGE);

        // Pupil highlight, offset up and toward the turn so it catches a
        // consistent light. It rides the blink with the eye, and disappears
        // entirely once the lid is nearly shut rather than sitting on a slit.
        if (c.eye_open > 0.45f) {
            gfx_fill_ellipse(ex + erx * 0.30f + c.facing * s, ey + gy - ery * 0.32f,
                             erx * 0.42f, ery * 0.34f, C_PUPIL);
        }
    }

    draw_mouth(cx, cy, s);
}
