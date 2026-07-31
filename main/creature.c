#include "creature.h"

#include <math.h>

#include "canvas.h"
#include "gfx.h"

#define PI 3.14159265f

// Palette from the reference art: flat fills, one darker tone for the underside,
// heavy near-black outline, and a cool cast shadow on the ground.
static uint16_t C_BG, C_BODY, C_SHADE, C_MOUTH, C_TONGUE, C_EDGE, C_GROUND;

// ---------------------------------------------------------------------------
// Pseudo-3D
//
// The creature is modelled as points in its own 3D space — x across, y up from
// the ground, z forward — then yawed about the vertical axis and projected. That
// is enough to turn it convincingly without any of the machinery of real 3D: no
// mesh, no z-buffer, no shading model, and the flat cartoon look survives intact.
//
// Yaw is capped at +/-60 degrees so the creature never shows its back. Past that
// the trick needs genuine occlusion reasoning about which features are in front,
// and the back would have to be designed. Distance is faked by scale instead:
// further away is smaller and higher up the screen.
// ---------------------------------------------------------------------------

#define YAW_LIMIT_DEG 60.0f

// Ground plane. Depth 0 is the far edge, 1 the near edge.
#define GROUND_FAR_Y   126.0f
#define GROUND_NEAR_Y  224.0f
#define SCALE_FAR      0.62f
#define SCALE_NEAR     1.00f
// Horizontal room shrinks with distance, which reads as perspective.
#define SPAN_FAR       0.62f
#define SPAN_NEAR      1.00f
// How much a point's depth within the body lifts it up the screen.
#define Z_RISE         0.34f

// Body proportions, in local units at scale 1.
#define BODY_Y      60.0f
#define BODY_RX     46.0f
#define BODY_RY     38.0f
#define HEAD_Y      92.0f
#define HEAD_Z      30.0f
#define HEAD_R      36.0f
#define LEG_SPREAD  20.0f
#define LEG_FRONT_Z  24.0f
#define LEG_BACK_Z  -22.0f
#define LEG_TOP_Y    34.0f
#define WING_X       13.0f
#define WING_Y       84.0f
#define WING_Z       -8.0f
#define SHADOW_DEPTH 16.0f

typedef struct {
    // Continuous emotion. Everything expressive is derived from these two.
    float valence;   // 0 distressed .. 1 delighted
    float arousal;   // 0 calm .. 1 excited

    // Where it is in the world, both 0..1. wz is depth: 0 far, 1 near.
    float wx, wz;
    float tx, tz;          // wander target
    float wander_timer;
    float walk_phase;
    float speed;

    float yaw;             // -1 .. 1, scaled to +/-YAW_LIMIT_DEG
    float yaw_target;

    // Rendering parameters, all smoothed toward targets rather than snapped.
    float breathe;
    float wobble_phase;
    float squash;
    float mouth_open;
    float smile;
    float eye_open;
    float gaze_x, gaze_y;

    // Blink and saccade timers, randomised every time: a fixed-period blink
    // reads as a machine immediately.
    float blink_timer, blink_t;
    float saccade_timer;
    float gaze_tx, gaze_ty;

    bool was_touched;
} creature_t;

static creature_t c;

// Projection state, recomputed once per frame.
static float p_cx, p_cy, p_scale, p_sin, p_cos;

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

// Local (x across, y up, z forward) to screen. Also returns the depth so callers
// can order their drawing and scale near parts up slightly.
static gfx_pt_t project(float lx, float ly, float lz, float *out_z)
{
    float xr = lx * p_cos + lz * p_sin;
    float zr = -lx * p_sin + lz * p_cos;
    if (out_z) {
        *out_z = zr;
    }
    // Depth within the body nudges scale, which sells the turn: the near
    // shoulder grows a little as the creature swings round.
    float s = p_scale * (1.0f + zr * 0.0016f);
    return (gfx_pt_t){p_cx + xr * s, p_cy - ly * s - zr * Z_RISE * s};
}

static float depth_scale(float lz)
{
    return p_scale * (1.0f + lz * 0.0016f);
}

void creature_init(void)
{
    C_BG     = display_rgb(246, 244, 248);
    C_BODY   = display_rgb(186, 108, 190);
    C_SHADE  = display_rgb(148, 76, 154);
    C_MOUTH  = display_rgb(74, 38, 82);
    C_TONGUE = display_rgb(214, 156, 208);
    C_EDGE   = display_rgb(24, 14, 28);
    C_GROUND = display_rgb(176, 178, 206);

    c.valence = 0.6f;
    c.arousal = 0.25f;
    c.eye_open = 1.0f;
    c.mouth_open = 0.35f;
    c.smile = 0.6f;
    c.blink_timer = 2.0f;
    c.saccade_timer = 1.0f;
    c.wx = c.tx = 0.5f;
    c.wz = c.tz = 0.6f;
    c.wander_timer = 2.0f;
}

// Screen position of a point on the ground, given world coords.
static void ground_to_screen(float wx, float wz, float *sx, float *sy, float *scale)
{
    float s = SCALE_FAR + (SCALE_NEAR - SCALE_FAR) * wz;
    float span = SPAN_FAR + (SPAN_NEAR - SPAN_FAR) * wz;
    float half = (DISPLAY_WIDTH * 0.5f - 34.0f) * span;
    *sx = DISPLAY_WIDTH * 0.5f + (wx - 0.5f) * 2.0f * half;
    *sy = GROUND_FAR_Y + (GROUND_NEAR_Y - GROUND_FAR_Y) * wz;
    *scale = s;
}

void creature_update(float dt, bool touched, int touch_x, int touch_y)
{
    c.breathe += dt * 1.1f;
    c.wobble_phase += dt * 0.55f;

    // --- reflex layer: immediate, no decisions ---------------------------
    // Being poked is a jolt first and a feeling second. The squash and the gaze
    // snap happen on the same frame as the touch; the mood catches up after.
    if (touched) {
        float sx, sy, sc;
        ground_to_screen(c.wx, c.wz, &sx, &sy, &sc);
        float body_y = sy - BODY_Y * sc;

        c.gaze_tx = clampf(((float)touch_x - sx) / (BODY_RX * sc), -1.0f, 1.0f);
        c.gaze_ty = clampf(((float)touch_y - body_y) / (BODY_RY * sc), -1.0f, 1.0f);
        c.saccade_timer = 0.6f;

        // Turn to face whoever poked it. A creature that looks at you reads as
        // far more alive than one that only swivels its eyes.
        c.yaw_target = clampf(((float)touch_x - sx) / 90.0f, -1.0f, 1.0f);

        if (!c.was_touched) {
            c.squash += 0.5f;
            c.arousal = clampf(c.arousal + 0.45f, 0, 1);
            c.valence = clampf(c.valence + 0.12f, 0, 1);
            c.wander_timer = 1.6f;      // stay put and enjoy it
            c.speed = 0.0f;
        }
        c.arousal = clampf(c.arousal + dt * 0.35f, 0, 1);
    }
    c.was_touched = touched;

    // --- wandering -------------------------------------------------------
    c.wander_timer -= dt;
    if (c.wander_timer <= 0.0f) {
        c.tx = 0.12f + gfx_randf() * 0.76f;
        c.tz = 0.15f + gfx_randf() * 0.80f;
        // Excited creatures move more often and settle for less time.
        c.wander_timer = 2.0f + gfx_randf() * 5.0f - c.arousal * 1.2f;
    }

    float dx = c.tx - c.wx, dz = c.tz - c.wz;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > 0.02f && !touched) {
        float want = 0.10f + c.arousal * 0.16f;
        c.speed = approach(c.speed, want, 3.0f, dt);
        c.wx += dx / dist * c.speed * dt;
        c.wz += dz / dist * c.speed * dt;
        // Face the direction of travel, but never far enough to show its back.
        c.yaw_target = clampf(dx / 0.22f, -1.0f, 1.0f);
        c.walk_phase += dt * (2.2f + c.speed * 6.0f);
    } else {
        c.speed = approach(c.speed, 0.0f, 4.0f, dt);
    }
    c.wx = clampf(c.wx, 0.08f, 0.92f);
    c.wz = clampf(c.wz, 0.10f, 0.95f);
    c.yaw = approach(c.yaw, c.yaw_target, 3.0f, dt);

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
        c.blink_timer = 1.4f + gfx_randf() * 4.0f - c.arousal * 0.8f;
    }
    if (c.blink_t > 0.0f) {
        c.blink_t -= dt * 9.0f;
        if (c.blink_t < 0.0f) { c.blink_t = 0.0f; }
    }
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

// A tapered limb between two projected points. The spine curves, the width falls
// off as a cosine so the edges never kink, and the tip is capped with a
// semicircle — nothing on the creature is a hard corner.
static void draw_limb(gfx_pt_t base, gfx_pt_t tip, float w, float bend, float taper)
{
#define LIMB_STEPS 10
    float dx = tip.x - base.x, dy = tip.y - base.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.5f || w < 1.0f) {
        return;
    }
    float ctlx = base.x + dx * 0.5f - dy / len * bend;
    float ctly = base.y + dy * 0.5f + dx / len * bend;

    static gfx_pt_t p[GFX_MAX_POLY_PTS];   // static: see the note in gfx.c
    int n = 0;

    float sx[LIMB_STEPS + 1], sy[LIMB_STEPS + 1], sw[LIMB_STEPS + 1];
    float tanx[LIMB_STEPS + 1], tany[LIMB_STEPS + 1];
    for (int i = 0; i <= LIMB_STEPS; i++) {
        float u = 0.88f * (float)i / (float)LIMB_STEPS;
        float m = 1.0f - u;
        sx[i] = m * m * base.x + 2.0f * m * u * ctlx + u * u * tip.x;
        sy[i] = m * m * base.y + 2.0f * m * u * ctly + u * u * tip.y;
        // taper 0 keeps an even thickness, 1 comes to a point.
        sw[i] = w * (1.0f - taper * (u / 0.88f));
    }
    for (int i = 0; i <= LIMB_STEPS; i++) {
        int a = (i > 0) ? i - 1 : i;
        int b = (i < LIMB_STEPS) ? i + 1 : i;
        float ux = sx[b] - sx[a], uy = sy[b] - sy[a];
        float ul = sqrtf(ux * ux + uy * uy);
        if (ul < 0.0001f) { ul = 1.0f; }
        tanx[i] = ux / ul;
        tany[i] = uy / ul;
    }

    for (int i = 0; i <= LIMB_STEPS; i++) {
        p[n++] = (gfx_pt_t){sx[i] - tany[i] * sw[i], sy[i] + tanx[i] * sw[i]};
    }
    // Swept from the left normal through the tangent to the right normal, built
    // from the vectors rather than an angle so the winding always matches the
    // two sides. Get that wrong and the polygon self-intersects, which the
    // even-odd fill leaves as a hole showing the background through the limb.
    {
        const int cap = 6;
        float nlx = -tany[LIMB_STEPS], nly = tanx[LIMB_STEPS];
        for (int i = 1; i < cap; i++) {
            float s = (float)i / (float)cap;
            float cs = cosf(s * PI), sn = sinf(s * PI);
            p[n++] = (gfx_pt_t){
                sx[LIMB_STEPS] + (nlx * cs + tanx[LIMB_STEPS] * sn) * sw[LIMB_STEPS],
                sy[LIMB_STEPS] + (nly * cs + tany[LIMB_STEPS] * sn) * sw[LIMB_STEPS]};
        }
    }
    for (int i = LIMB_STEPS; i >= 0; i--) {
        p[n++] = (gfx_pt_t){sx[i] + tany[i] * sw[i], sy[i] - tanx[i] * sw[i]};
    }

    gfx_fill_poly_outlined(p, n, C_BODY, C_EDGE, 3.0f * p_scale);
}

// One leg, lifted through its share of the gait when walking.
static void draw_leg(float lx, float lz, float phase_off, float lift_amt)
{
    float ph = c.walk_phase + phase_off;
    float lift = lift_amt * fmaxf(0.0f, sinf(ph * 2.0f * PI)) * 9.0f;
    float swing = lift_amt * cosf(ph * 2.0f * PI) * 7.0f;

    float z;
    gfx_pt_t top = project(lx, LEG_TOP_Y, lz, &z);
    gfx_pt_t foot = project(lx, lift, lz + swing, NULL);
    draw_limb(top, foot, 9.5f * depth_scale(lz), 2.0f, 0.35f);
}

// One wing. A mirrored pair, so exactly the same problem the legs already solve:
// the yaw transform swings one forward and the other back, and the caller draws
// each on the correct side of the body. Wings flare with arousal, which gives
// the emotion a second channel besides the mouth.
static void draw_wing(float side, float *out_z)
{
    float flare = 0.25f + c.arousal * 0.75f;
    float flap = sinf(c.breathe * 1.7f + side) * (0.06f + c.arousal * 0.16f);

    float bx = side * WING_X;
    gfx_pt_t base = project(bx, WING_Y, WING_Z, out_z);
    gfx_pt_t tip = project(bx + side * (34.0f + flare * 16.0f),
                           WING_Y + 52.0f + flare * 16.0f + flap * 44.0f,
                           WING_Z - 30.0f, NULL);
    draw_limb(base, tip, 11.0f * depth_scale(WING_Z), side * 7.0f, 0.82f);
}

static void draw_face(void)
{
    float s = p_scale;
    float hz;
    gfx_pt_t head = project(0.0f, HEAD_Y, HEAD_Z, &hz);

    // Yaw slides the face across the head and foreshortens it. cos(yaw) is how
    // much of the face is still turned toward us.
    float face = p_cos;
    float slide = p_sin * HEAD_R * 0.55f;

    // Mouth: a scalloped cavity. Few, large teeth — many small ones read as
    // noise at this resolution rather than as a grin.
    const int teeth = 5;
    const float half_w = (30.0f + c.smile * 5.0f) * s * face;
    const float depth = (22.0f + c.mouth_open * 26.0f) * s;
    const float tooth = 6.5f * s;
    float mx = head.x + slide * s * 0.4f;
    float my = head.y + 9.0f * s;

    if (half_w > 3.0f) {
        static gfx_pt_t p[GFX_MAX_POLY_PTS];
        int n = 0;
        const int per_tooth = 5, steps = teeth * per_tooth;
        float corner_y = my - c.smile * 4.0f * s;

        for (int i = 0; i <= steps; i++) {
            float u = (float)i / (float)steps;
            float bump = 0.5f - 0.5f * cosf(u * (float)teeth * 2.0f * PI);
            p[n++] = (gfx_pt_t){mx - half_w + 2.0f * half_w * u,
                                corner_y + (my - corner_y + 4.0f * s) * sinf(PI * u)
                                    + tooth * bump};
        }
        for (int i = steps; i >= 0; i--) {
            float u = (float)i / (float)steps;
            float bump = 0.5f - 0.5f * cosf(u * (float)teeth * 2.0f * PI);
            p[n++] = (gfx_pt_t){mx - half_w + 2.0f * half_w * u,
                                corner_y + (my - corner_y + depth) * sinf(PI * u)
                                    - tooth * bump};
        }
        gfx_fill_poly_outlined(p, n, C_MOUTH, C_EDGE, 2.5f * s);

        if (c.mouth_open > 0.32f) {
            float t = (c.mouth_open - 0.32f) / 0.68f;
            gfx_fill_ellipse(mx, my + depth * 0.55f,
                             (11.0f * t + 5.0f) * s * face, (7.0f * t + 3.0f) * s,
                             C_TONGUE);
        }
    }

    // Eyes. Yaw moves them and squeezes the far one toward the silhouette; past
    // about 50 degrees it has gone round the side and is simply not drawn.
    float ey = head.y - 13.0f * s;
    float gx = c.gaze_x * 2.6f * s, gy = c.gaze_y * 2.0f * s;
    for (int i = 0; i < 2; i++) {
        float side = (i == 0) ? -1.0f : 1.0f;
        float lxs = side * 14.0f;
        float ex = head.x + (lxs * p_cos + HEAD_Z * 0.0f) * s + slide * s * 0.5f + gx;
        // How square-on this eye is: shrinks as it rotates away.
        float openness = p_cos + side * p_sin * 0.75f;
        if (openness <= 0.12f) {
            continue;
        }
        gfx_fill_ellipse(ex, ey + gy,
                         4.2f * s * clampf(openness, 0.0f, 1.2f),
                         5.6f * s * c.eye_open, C_EDGE);
    }
}

void creature_draw(void)
{
    display_fill(C_BG);

    float sx, sy, sc;
    ground_to_screen(c.wx, c.wz, &sx, &sy, &sc);

    float yaw_rad = c.yaw * YAW_LIMIT_DEG * PI / 180.0f;
    p_sin = sinf(yaw_rad);
    p_cos = cosf(yaw_rad);
    p_scale = sc;
    p_cx = sx;

    // Breathing and the walk bob both move the whole body, so they ride on the
    // projection origin rather than being applied per shape.
    float breath = sinf(c.breathe) * 0.018f;
    float bob = sinf(c.walk_phase * 4.0f * PI) * 2.2f * c.speed * 8.0f;
    p_cy = sy + bob * sc;

    // Cast shadow on the ground: an ellipse that tightens as the creature lifts.
    gfx_fill_ellipse(sx, sy + 2.0f * sc, 40.0f * sc, 11.0f * sc, C_GROUND);

    // All four legs go down before the body, so the body covers where they
    // join and only the parts below it show — which is what makes them read as
    // legs rather than shapes stuck on the front.
    draw_leg(-LEG_SPREAD, LEG_BACK_Z, 0.00f, c.speed * 9.0f);
    draw_leg( LEG_SPREAD, LEG_BACK_Z, 0.50f, c.speed * 9.0f);
    draw_leg(-LEG_SPREAD, LEG_FRONT_Z, 0.50f, c.speed * 9.0f);
    draw_leg( LEG_SPREAD, LEG_FRONT_Z, 0.00f, c.speed * 9.0f);

    // Wings. Which one is behind the body depends on the yaw, so each is drawn
    // on the correct side of it — this pass takes whichever has swung backward.
    float wing_z[2];
    for (int i = 0; i < 2; i++) {
        float side = (i == 0) ? -1.0f : 1.0f;
        project(side * WING_X, WING_Y, WING_Z, &wing_z[i]);
    }
    for (int i = 0; i < 2; i++) {
        if (wing_z[i] < 0.0f) {
            draw_wing((i == 0) ? -1.0f : 1.0f, NULL);
        }
    }

    // Head and body share one silhouette. Both outlines go down first, then both
    // fills — otherwise whichever is drawn second cuts its own outline across the
    // other, and they read as two stuck-together shapes instead of one creature.
    const int BODY_PTS = 48, HEAD_PTS = 40;
    static gfx_pt_t body[GFX_MAX_POLY_PTS], head[GFX_MAX_POLY_PTS];
    static gfx_pt_t edge_buf[GFX_MAX_POLY_PTS];
    float bz, hz;
    gfx_pt_t bc = project(0.0f, BODY_Y, 0.0f, &bz);
    gfx_pt_t hc = project(0.0f, HEAD_Y, HEAD_Z, &hz);
    float hs = depth_scale(HEAD_Z);
    float brx = BODY_RX * sc * (1.0f + breath * 0.5f + c.squash * 0.10f);
    float bry = BODY_RY * sc * (1.0f + breath - c.squash * 0.13f);

    int bn = gfx_blob_points(body, BODY_PTS, bc.x, bc.y, brx, bry,
                             0.0f, 0.016f, c.wobble_phase, 0.10f);
    int hn = gfx_blob_points(head, HEAD_PTS, hc.x, hc.y,
                             HEAD_R * hs * (1.0f + breath * 0.3f),
                             (HEAD_R - 3.0f) * hs, 0.0f, 0.014f,
                             c.wobble_phase * 1.3f, 0.12f);

    gfx_poly_expand(head, edge_buf, hn, 3.0f * hs);
    gfx_fill_poly(edge_buf, hn, C_EDGE);
    gfx_poly_expand(body, edge_buf, bn, 3.0f * sc);
    gfx_fill_poly(edge_buf, bn, C_EDGE);
    gfx_fill_poly(head, hn, C_BODY);
    gfx_fill_poly(body, bn, C_BODY);

    // Underside shadow: the body's lower arc closed off by the same arc pulled
    // inward, so both edges follow the silhouette and the shading curves along
    // the body instead of cutting across it at an angle.
    {
        const int half = BODY_PTS / 2;
        static gfx_pt_t sh[GFX_MAX_POLY_PTS];
        int m = 0;
        for (int i = 0; i <= half; i++) {
            sh[m++] = body[i];
        }
        for (int i = half; i >= 0; i--) {
            // Depth fades to nothing at the ends so the crescent tapers to
            // points rather than being cut off with a blunt vertical edge.
            float u = (float)i / (float)half;
            float d = SHADOW_DEPTH * sc * sinf(PI * u);
            float ddx = body[i].x - bc.x, ddy = body[i].y - bc.y;
            float dd = sqrtf(ddx * ddx + ddy * ddy);
            float k = (dd > 0.001f) ? (dd - d) / dd : 0.0f;
            if (k < 0.0f) { k = 0.0f; }
            sh[m++] = (gfx_pt_t){bc.x + ddx * k, bc.y + ddy * k};
        }
        gfx_fill_poly(sh, m, C_SHADE);
    }

    // ...and the wing that swung forward goes over it.
    for (int i = 0; i < 2; i++) {
        if (wing_z[i] >= 0.0f) {
            draw_wing((i == 0) ? -1.0f : 1.0f, NULL);
        }
    }

    draw_face();
}
