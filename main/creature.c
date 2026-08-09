#include "creature.h"

#include <math.h>

#include "canvas.h"
#include "gfx.h"

#define PI 3.14159265f

// Palette lifted from the reference art: flat fills, one darker tone for the
// underside, heavy near-black outline.
static uint16_t C_BG, C_BODY, C_SHADE, C_MOUTH, C_TONGUE, C_EDGE, C_PUPIL, C_FOOT;

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
    float squash_velocity;
    float mouth_open;
    float smile;
    float eye_open;
    float gaze_x, gaze_y;
    float tap_x, tap_y;
    float tap_pulse;
    bool tap_on_body;

    // `facing` is the signed Y turn: at 0 the tail is behind the body and
    // hidden; away from 0 it emerges opposite the look direction.
    float facing, facing_target;
    float turn_timer;

    // Blink and saccade timers. Both intervals are randomised every time: a
    // fixed-period blink reads as a machine immediately.
    float blink_timer, blink_t;
    float saccade_timer;
    float gaze_tx, gaze_ty;

    bool was_on_body;
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

// The framebuffer is opaque RGB565 — there is no alpha channel and nothing
// blends at draw time. Where a shape always sits on a known background, the
// blend can simply be resolved once at startup and stored as a flat colour.
static inline uint8_t mix8(uint8_t fg, uint8_t bg, float a)
{
    return (uint8_t)(fg * a + bg * (1.0f - a) + 0.5f);
}

void creature_init(void)
{
    C_BG     = display_rgb(246, 244, 248);
    C_BODY   = display_rgb(186, 108, 190);
    C_SHADE  = display_rgb(148, 76, 154);
    // A shade under the underside tone: the feet sit below the body and read
    // as being in its shadow, which also stops them competing with the face.
    C_FOOT   = display_rgb(132, 66, 138);
    C_MOUTH  = display_rgb(74, 38, 82);
    C_TONGUE = display_rgb(205, 145, 200);
    C_EDGE   = display_rgb(24, 14, 28);
    // 90% white over the eye colour, pre-blended.
    const float pupil_alpha = 0.90f;
    C_PUPIL = display_rgb(mix8(255, 24, pupil_alpha),
                          mix8(255, 14, pupil_alpha),
                          mix8(255, 28, pupil_alpha));

    c.valence = 0.6f;
    c.arousal = 0.25f;
    c.eye_open = 1.0f;
    c.mouth_open = 0.35f;
    c.smile = 0.6f;
    c.tap_pulse = 0.0f;
    c.blink_timer = 2.0f;
    c.saccade_timer = 1.0f;
    c.facing = c.facing_target = 1.0f;
    c.turn_timer = 3.0f;
}

// Where the body sits on screen this frame. Shared by update and draw so that
// hit-testing a touch and rendering can never disagree about where it is.
static void body_frame(float *cx, float *cy, float *rx, float *ry, float *scale)
{
    // A fixed, slightly distant resting scale. Breathing and touch squash can
    // change the silhouette, but the pet never drifts toward or away from you.
    const float s = 0.80f;
    float breath = sinf(c.breathe) * 0.022f;
    *scale = s;
    *cx = BODY_CX;
    *cy = BODY_CY + c.squash * 8.0f * s;
    *rx = BODY_RX * s * (1.0f + breath * 0.5f + c.squash * 0.10f);
    *ry = BODY_RY * s * (1.0f + breath - c.squash * 0.13f);
}

bool creature_contains_point(int x, int y)
{
    float cx, cy, rx, ry, scale;
    body_frame(&cx, &cy, &rx, &ry, &scale);
    float nx = ((float)x - cx) / rx;
    float ny = ((float)y - cy) / ry;
    return (nx * nx + ny * ny) <= 1.0f;
}

void creature_update(float dt, bool touched, int touch_x, int touch_y)
{
    c.breathe += dt * 1.1f;
    c.wobble_phase += dt * 0.55f;

    // --- reflex layer: immediate, no decisions ---------------------------
    // Being poked is a jolt first and a feeling second. Gaze has a fast target
    // response; the body carries the physical response through a spring.
    bool on_body = touched && creature_contains_point(touch_x, touch_y);
    if (touched) {
        float cx, cy, rx, ry, s;
        body_frame(&cx, &cy, &rx, &ry, &s);

        float nx = ((float)touch_x - cx) / rx;
        float ny = ((float)touch_y - cy) / ry;

        c.saccade_timer = 0.6f;

        // The visual contact response belongs to the panel, so it appears for
        // every new touch — whether it lands on the creature or the backdrop.
        if (!c.was_touched) {
            c.tap_x = (float)touch_x;
            c.tap_y = (float)touch_y;
            c.tap_pulse = 1.0f;
            c.tap_on_body = on_body;
        }

        if (on_body) {
            // Touched. React physically.
            c.gaze_tx = clampf(nx, -1.0f, 1.0f);
            c.gaze_ty = clampf(ny, -1.0f, 1.0f);
            c.lean = approach(c.lean, c.gaze_tx * 11.0f, 9.5f, dt);
            if (fabsf(c.gaze_tx) > 0.08f) {
                // Positive facing is looking right. The tail is drawn at
                // `cx - facing * …`, keeping it on the opposite side.
                c.facing_target = (c.gaze_tx > 0.0f) ? 1.0f : -1.0f;
            }
            c.turn_timer = 2.5f;

            if (!c.was_on_body) {
                // An impulse into a damped spring is smooth from the first
                // frame, then gives a tiny elastic rebound as it settles.
                c.squash_velocity += 9.0f;
                c.arousal = clampf(c.arousal + 0.50f, 0, 1);
                c.valence = clampf(c.valence + 0.12f, 0, 1);
            }
            c.arousal = clampf(c.arousal + dt * 0.35f, 0, 1);
        } else {
            // Something happening nearby but not to it. Watch, do not flinch.
            // Divided by a screen-sized span rather than the body radius so the
            // look is graded by direction instead of pinning to the limit the
            // moment the point leaves the body.
            c.gaze_tx = clampf(((float)touch_x - cx) / 95.0f, -1.0f, 1.0f);
            c.gaze_ty = clampf(((float)touch_y - cy) / 75.0f, -1.0f, 1.0f);
            c.lean = approach(c.lean, c.gaze_tx * 5.0f, 5.0f, dt);
            if (fabsf(c.gaze_tx) > 0.08f) {
                c.facing_target = (c.gaze_tx > 0.0f) ? 1.0f : -1.0f;
                c.turn_timer = 2.5f;
            }
            // Mild curiosity only — enough to widen the eyes, not to startle.
            c.arousal = clampf(c.arousal + dt * 0.18f, 0, 1);
        }
    } else {
        c.lean = approach(c.lean, sinf(c.breathe * 0.31f) * 3.0f, 1.5f, dt);
    }
    c.was_on_body = on_body;
    c.was_touched = touched;
    c.tap_pulse -= dt * 3.4f;
    if (c.tap_pulse < 0.0f) { c.tap_pulse = 0.0f; }

    // --- idle turning -----------------------------------------------------
    // The interval is randomised like the blink: a fixed period starts to read
    // as a mechanism rather than a creature.
    c.turn_timer -= dt;
    if (c.turn_timer <= 0.0f) {
        c.facing_target = (gfx_randf() < 0.5f) ? -1.0f : 1.0f;
        c.turn_timer = 2.5f + gfx_randf() * 6.0f - c.arousal * 1.5f;
    }
    c.facing = approach(c.facing, c.facing_target, 2.2f + c.arousal * 2.0f, dt);

    // --- cognition layer: slow drift back to baseline --------------------
    c.arousal = approach(c.arousal, 0.22f, 0.35f, dt);
    c.valence = approach(c.valence, 0.55f, 0.12f, dt);
    // A lightly underdamped spring replaces a hard squash followed by a
    // mechanical exponential decay. It makes each poke feel soft and alive.
    c.squash_velocity += -54.0f * c.squash * dt;
    c.squash_velocity *= expf(-9.0f * dt);
    c.squash += c.squash_velocity * dt;

    // --- emotion drives the visible parameters ---------------------------
    float mouth_target = 0.20f + c.arousal * 0.55f + c.valence * 0.12f;
    float smile_target = 0.15f + c.valence * 0.85f;
    // The mouth carries the creature's expression, so it should follow a
    // feeling rather than jump to it. Slower easing makes each change read as
    // a soft shift through the face instead of a separate animation state.
    c.mouth_open = approach(c.mouth_open, mouth_target, 3.8f, dt);
    c.smile = approach(c.smile, smile_target, 2.4f, dt);

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

static void draw_blade(float bx, float by, float tipx, float tipy, float w, float bend,
                       uint16_t fill, bool outlined, float lower_bias)
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
        float lnx = -tany[i], lny = tanx[i];
        if (lny < 0.0f) { lnx = -lnx; lny = -lny; }
        float ox = lnx * sw[i] * lower_bias;
        float oy = lny * sw[i] * lower_bias;
        p[n++] = (gfx_pt_t){sx[i] + ox - tany[i] * sw[i],
                            sy[i] + oy + tanx[i] * sw[i]};
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
        float lox = nlx, loy = nly;
        if (loy < 0.0f) { lox = -lox; loy = -loy; }
        float ox = lox * sw[steps] * lower_bias;
        float oy = loy * sw[steps] * lower_bias;
        for (int i = 1; i < cap; i++) {
            float s = (float)i / (float)cap;
            float cs = cosf(s * PI), sn = sinf(s * PI);
            p[n++] = (gfx_pt_t){sx[steps] + ox + (nlx * cs + tanx[steps] * sn) * sw[steps],
                                sy[steps] + oy + (nly * cs + tany[steps] * sn) * sw[steps]};
        }
    }
    // ...and back along the right.
    for (int i = steps; i >= 0; i--) {
        float lnx = -tany[i], lny = tanx[i];
        if (lny < 0.0f) { lnx = -lnx; lny = -lny; }
        float ox = lnx * sw[i] * lower_bias;
        float oy = lny * sw[i] * lower_bias;
        p[n++] = (gfx_pt_t){sx[i] + ox + tany[i] * sw[i],
                            sy[i] + oy - tanx[i] * sw[i]};
    }

    if (outlined) {
        gfx_fill_poly_outlined(p, n, fill, C_EDGE, 3.0f);
    } else {
        gfx_fill_poly(p, n, fill);
    }
}

// `cx`/`cy` are the body centre and `s` the shared creature scale, so the mouth
// remains correctly attached when the body breathes or squashes.
static void draw_mouth(float cx, float cy, float s)
{
    const float turn = 1.0f - 0.16f * fabsf(c.facing);
    float mx = cx + (c.lean * 0.5f + c.facing * 7.0f) * s;

    const int teeth = 4;
    const int top_steps = teeth * 10;
    const int side_steps = 12;
    const int bowl_steps = 32;
    const float top_rx = (61.0f + c.smile * 7.0f) * s * turn;
    const float bowl_rx = top_rx - 8.0f * s;
    const float top_y = cy - 15.0f * s;
    const float bowl_y = top_y + (17.0f + c.mouth_open * 4.0f) * s;
    const float bowl_depth = (28.0f + c.mouth_open * 18.0f) * s;
    const float tooth_depth = 8.5f * s;
    const float sag = (3.0f + c.smile * 2.0f) * s;
    const float corner_dx = 8.0f * s;
    const float side_tangent = 12.0f * s;

    static gfx_pt_t p[GFX_MAX_POLY_PTS];
    int n = 0;

    // Four scalloped teeth across the top. At each end the tooth term has zero
    // slope, so the remaining tangent is known and can be matched by the side.
    for (int i = 0; i <= top_steps; i++) {
        float q = (float)i / (float)top_steps;
        float u = q * 2.0f - 1.0f;
        float raised = 0.5f - 0.5f * cosf(q * (float)teeth * 2.0f * PI);
        float arch = 1.0f - u * u;
        float y = top_y + sag * arch * arch
                        + tooth_depth * raised * raised;
        p[n++] = (gfx_pt_t){mx + u * top_rx, y};
    }

    // The cubic side leaves the top along its exact tangent and reaches the
    // bowl vertically. Matching both tangents removes the visible corner.
    gfx_pt_t right_top = p[n - 1];
    gfx_pt_t right_bowl = {mx + bowl_rx, bowl_y};
    gfx_pt_t right_c1 = {right_top.x + corner_dx, right_top.y};
    gfx_pt_t right_c2 = {right_bowl.x, right_bowl.y - side_tangent};
    for (int i = 1; i <= side_steps; i++) {
        float t = (float)i / (float)side_steps;
        float v = 1.0f - t;
        p[n++] = (gfx_pt_t){
            v * v * v * right_top.x
                + 3.0f * v * v * t * right_c1.x
                + 3.0f * v * t * t * right_c2.x
                + t * t * t * right_bowl.x,
            v * v * v * right_top.y
                + 3.0f * v * v * t * right_c1.y
                + 3.0f * v * t * t * right_c2.y
                + t * t * t * right_bowl.y,
        };
    }

    // One uninterrupted elliptical bowl, right to left.
    for (int i = 1; i <= bowl_steps; i++) {
        float a = PI * (float)i / (float)bowl_steps;
        p[n++] = (gfx_pt_t){
            mx + bowl_rx * cosf(a),
            bowl_y + bowl_depth * sinf(a),
        };
    }

    // Mirror the tangent-matched side on the left. The final curve sample is
    // omitted because the polygon close supplies the shared top point.
    gfx_pt_t left_bowl = p[n - 1];
    gfx_pt_t left_top = p[0];
    gfx_pt_t left_c1 = {left_bowl.x, left_bowl.y - side_tangent};
    gfx_pt_t left_c2 = {left_top.x - corner_dx, left_top.y};
    for (int i = 1; i < side_steps; i++) {
        float t = (float)i / (float)side_steps;
        float v = 1.0f - t;
        p[n++] = (gfx_pt_t){
            v * v * v * left_bowl.x
                + 3.0f * v * v * t * left_c1.x
                + 3.0f * v * t * t * left_c2.x
                + t * t * t * left_top.x,
            v * v * v * left_bowl.y
                + 3.0f * v * v * t * left_c1.y
                + 3.0f * v * t * t * left_c2.y
                + t * t * t * left_top.y,
        };
    }

    gfx_fill_poly_outlined(p, n, C_MOUTH, C_EDGE, 2.0f * s);

    if (c.mouth_open > 0.32f) {
        float t = (c.mouth_open - 0.32f) / 0.68f;
        gfx_fill_ellipse(mx - 4.0f * s, bowl_y + bowl_depth * 0.58f,
                         (26.0f * t + 10.0f) * s * turn, (15.0f * t + 5.0f) * s,
                         C_TONGUE);
    }
}

static void draw_tap_pulse(float s)
{
    if (c.tap_pulse <= 0.0f) {
        return;
    }

    // A contact is a fleeting disturbance, not a UI cursor: small bright
    // motes expand from the exact touch point and disappear within a third of
    // a second. On the creature they are bright; on the white canvas they use
    // the body colour so they remain visible without becoming a UI cursor.
    float age = 1.0f - c.tap_pulse;
    float radius = (3.0f + age * 17.0f) * s * 0.75f;
    float dot = (2.6f - age * 1.4f) * s * 0.75f;
    float phase = age * 0.55f;
    uint16_t colour = c.tap_on_body ? C_PUPIL : C_BODY;
    for (int layer = 0; layer < 2; layer++) {
        float layer_radius = radius * (layer ? 0.58f : 1.0f);
        float layer_dot = dot * (layer ? 0.78f : 1.0f);
        float layer_phase = phase + (layer ? PI / 8.0f : 0.0f);
        for (int i = 0; i < 8; i++) {
            float a = layer_phase + (float)i * PI / 4.0f;
            gfx_fill_ellipse(c.tap_x + cosf(a) * layer_radius,
                             c.tap_y + sinf(a) * layer_radius,
                             layer_dot, layer_dot, colour);
        }
    }
}

void creature_draw(void)
{
    display_fill(C_BG);

    // One fixed scale carries the whole creature. Breathing scales the body very
    // slightly; squash trades height for width so volume looks conserved when it
    // reacts.
    float cx, cy, rx, ry, s;
    body_frame(&cx, &cy, &rx, &ry, &s);

    // The tail sweeps to the side opposite `facing`, passing behind the body at
    // the midpoint. Thinning it as it crosses sells the Y turn rather than a
    // teleport, since an edge-on tail should almost vanish.
    {
        float f = c.facing;
        float w = 19.0f * s * (0.30f + 0.70f * fabsf(f));
        float bx = cx - f * 58.0f * s + c.lean * 0.6f;
        float by = cy - 30.0f * s;
        float tipx = cx - f * 96.0f * s + c.lean * 1.4f;
        float tipy = cy - 74.0f * s;
        // The regular tail keeps its outline. A narrow, unoutlined blade laid
        // into the lower edge is the shadow: roughly the lower quarter only.
        draw_blade(bx, by, tipx, tipy, w, f * 7.0f, C_BODY, true, 0.0f);
        // The shadow blade is one quarter as wide, so biasing its centre by
        // three of its own widths places it over the outer lower quarter of
        // the full tail rather than leaving it centred on the spine.
        draw_blade(bx, by, tipx, tipy, w * 0.25f, f * 7.0f, C_SHADE, false, 3.0f);
    }

    // Feet shift a little with the turn so the body does not look bolted down.
    float fx = c.facing * 6.0f * s;
    draw_blade(cx - 36.0f * s + fx + c.lean * 0.3f, cy + ry * 0.74f,
               cx - 62.0f * s + fx, cy + ry * 1.00f, 17.0f * s, 7.0f, C_FOOT, true, 0.0f);
    draw_blade(cx + 36.0f * s + fx + c.lean * 0.3f, cy + ry * 0.74f,
               cx + 64.0f * s + fx, cy + ry * 0.98f, 17.0f * s, -7.0f, C_FOOT, true, 0.0f);

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
    // A tap should make the face clearly orient toward the contact point, not
    // merely twitch by a pixel or two. These are still eased by gaze_x/y above.
    float gx = (c.gaze_x * 6.5f + c.lean * 0.6f + c.facing * 9.0f) * s;
    float gy = c.gaze_y * 4.5f * s;
    for (int i = 0; i < 2; i++) {
        float side = (i == 0) ? -1.0f : 1.0f;
        float squeeze = 1.0f - 0.35f * fabsf(c.facing) * ((side * c.facing > 0) ? 1.0f : 0.0f);
        float ex = cx + side * 27.0f * s + gx;
        float erx = 7.6f * s * squeeze, ery = 10.5f * s * c.eye_open;
        gfx_fill_ellipse(ex, ey + gy, erx, ery, C_EDGE);

        // Pupil highlight, offset up and toward the turn so it catches a
        // consistent light. It rides the blink with the eye, and disappears
        // entirely once the lid is nearly shut rather than sitting on a slit.
        if (c.eye_open > 0.45f) {
            // Tracking happens mostly here rather than by sliding the whole
            // eye: a pupil moving inside a fixed eye is what reads as looking
            // at something. Constrain its centre to an inset ellipse so the
            // highlight cannot cross the eye outline when it turns or looks
            // hard toward an edge.
            float prx = erx * 0.42f, pry = ery * 0.34f;
            float dx = erx * 0.30f + c.facing * s + c.gaze_x * erx * 0.48f;
            float dy = -ery * 0.32f + c.gaze_y * ery * 0.38f;
            float inner_rx = erx - prx - 0.5f * s;
            float inner_ry = ery - pry - 0.5f * s;
            float distance = sqrtf((dx / inner_rx) * (dx / inner_rx)
                                 + (dy / inner_ry) * (dy / inner_ry));
            if (distance > 1.0f) {
                dx /= distance;
                dy /= distance;
            }
            gfx_fill_ellipse(ex + dx, ey + gy + dy, prx, pry, C_PUPIL);
        }
    }

    draw_mouth(cx, cy, s);
    draw_tap_pulse(s);
}
