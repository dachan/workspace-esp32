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

// --- feeding and life --------------------------------------------------------
// A service control in the same spirit as the rotary encoder in AGENTS.md: it
// sits outside the pet fiction in the corner rather than pretending to be part
// of the animal, because "where the food comes from" has no honest in-body
// answer the way a poke does.
#define FEED_BTN_CX 30.0f
#define FEED_BTN_CY 30.0f
#define FEED_BTN_R  22.0f

#define LIFE_TICK_SECONDS 3600.0f   // one real hour
#define LIFE_DECAY_MIN    5.0f
#define LIFE_DECAY_MAX    10.0f
#define LIFE_FEED_AMOUNT  25.0f

// Below this, breathing slows and the body desaturates as life drains toward
// zero — both bottom out together at 1% life. See life_low_frac().
#define LIFE_LOW_THRESHOLD  50.0f
#define BREATH_RATE_NORMAL  1.1f    // matches the resting breathe rate below
#define BREATH_RATE_BARE    0.12f   // barely breathing, at 1% life
#define BODY_SAT_MIN        0.10f   // fraction of normal saturation at 1% life

#define FEED_FLIGHT_TIME 0.45f   // pellet in the air, button to mouth
#define FEED_ANIM_TOTAL  0.65f   // total lifetime including the chomp settle

#define LIFE_BAR_W 60.0f
#define LIFE_BAR_H 12.0f
#define LIFE_BAR_X ((float)DISPLAY_WIDTH - 16.0f - LIFE_BAR_W)
#define LIFE_BAR_Y 14.0f

// Debug aid only: knocks life down 5 points per press so the low-life states
// (slowed breathing, desaturated body) and death can be reached without an
// hour of real time. Bottom-right corner, well clear of the body and of the
// two real controls above. Pull this before considering the UI final.
#define DEBUG_STARVE_BTN_CX ((float)DISPLAY_WIDTH - 30.0f)
#define DEBUG_STARVE_BTN_CY ((float)DISPLAY_HEIGHT - 30.0f)
#define DEBUG_STARVE_BTN_R  22.0f
#define DEBUG_STARVE_DECREMENT 5.0f

// The death sequence: hold the frozen pose this long before it starts falling
// away, then ease `vanish` from 0 to 1 (or back to 0 on revival) at this rate.
// One curve, run forward or backward — see the `vanish` field.
#define DEATH_FREEZE_SECONDS 0.3f
#define VANISH_RATE          2.2f

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

    // Life drains on its own; feeding and the death it forestalls are the only
    // things that move it.
    float life;         // 0..100
    float life_timer;   // counts down to the next hourly loss
    bool dead;

    // The death sequence: frozen on the spot for a beat, then presses flat
    // onto the ground like a sheet of paper falling — the view is at table
    // height, so once it's paper-thin there's no cross-section left to see,
    // and it simply fades from there. One continuous parameter drives all of
    // it, so reviving mid-fall is just the same curve run in reverse rather
    // than a separate animation.
    float vanish;         // 0 present .. 1 fully flat and faded
    float death_timer;    // seconds since death, for the freeze hold below

    // How long this life has lasted. Counts up from 0 at boot and again from
    // each revival; the value is copied into death_survival_seconds the
    // instant it dies, so the readout has something fixed to show instead of
    // a timer still running against a body that no longer moves.
    float survival_timer;
    float death_survival_seconds;

    bool button_pressed;      // this frame, for the button's own press feedback
    bool feed_anim_active;
    float feed_t;              // seconds since the button was pressed
    bool feed_chomped;         // chomp impulse fires once per animation

    bool debug_starve_pressed;   // this frame, for its own press feedback
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

// Colour only starts fading in the last stretch of vanish, once the fall has
// finished and it's sitting fully flat — so it's visibly a flattened paper
// shape for a beat before it fades, rather than fading while still falling.
static inline float vanish_fade_frac(float vanish)
{
    return clampf((vanish - 0.7f) / 0.3f, 0.0f, 1.0f);
}

// Bends a point vertically by a parabola in its horizontal distance from the
// mouth centre, so the corners swing up for a smile and down for a frown while
// the centre stays put. `lift` is positive for a smile.
//
// This is a pure vertical shear: every point at the same x moves by the same
// amount, so vertical ordering is preserved and the outline can never fold
// over itself no matter how hard it is bent. That is why it is applied as a
// post-pass over the finished point list rather than threaded through the
// mouth's generation maths, where it would have to be reasoned about
// separately for the top edge, the corner beziers and the bowl.
static inline gfx_pt_t bend_pt(float x, float y, float mx, float half_w, float lift)
{
    float u = (half_w > 0.001f) ? (x - mx) / half_w : 0.0f;
    return (gfx_pt_t){x, y - lift * u * u};
}

// Scales a point's height toward an anchor line without touching x or
// rotating anything — the first half of the death sequence's flatten, a
// cutout collapsing down onto the ground rather than toppling over. ky 1.0 =
// untouched, 0.0 = pressed flat onto anchor_y.
static inline gfx_pt_t flat_pt(float x, float y, float anchor_y, float ky)
{
    return (gfx_pt_t){x, anchor_y + (y - anchor_y) * ky};
}

// 0 at/above LIFE_LOW_THRESHOLD, ramping to 1 as life bottoms out at 1%. Both
// the breathing slowdown and the body desaturation read from this so the two
// bottom out together rather than drifting out of sync.
static inline float life_low_frac(float life)
{
    return clampf((LIFE_LOW_THRESHOLD - life) / (LIFE_LOW_THRESHOLD - 1.0f), 0.0f, 1.0f);
}

// Background RGB, matching the literal in creature_init — kept as raw
// components too since tint_rgb() needs to blend toward it.
#define BG_R 246
#define BG_G 244
#define BG_B 248

// Desaturates a colour toward its own luminance (sat_factor 1.0 = untouched,
// 0.0 = grayscale), then fades what's left toward the background (fade 0.0 =
// untouched, 1.0 = invisible against it). One pass so low life and dying both
// read from the same pipeline instead of two separate colour states.
static inline uint16_t tint_rgb(uint8_t r, uint8_t g, uint8_t b, float sat_factor, float fade)
{
    float gray = 0.299f * (float)r + 0.587f * (float)g + 0.114f * (float)b;
    float dr = gray + ((float)r - gray) * sat_factor;
    float dg = gray + ((float)g - gray) * sat_factor;
    float db = gray + ((float)b - gray) * sat_factor;
    float fr = dr + ((float)BG_R - dr) * fade;
    float fg = dg + ((float)BG_G - dg) * fade;
    float fb = db + ((float)BG_B - db) * fade;
    return display_rgb((uint8_t)clampf(fr, 0.0f, 255.0f),
                       (uint8_t)clampf(fg, 0.0f, 255.0f),
                       (uint8_t)clampf(fb, 0.0f, 255.0f));
}

// The framebuffer is opaque RGB565 — there is no alpha channel and nothing
// blends at draw time. Where a shape always sits on a known background, the
// blend can simply be resolved once at startup and stored as a flat colour.
static inline uint8_t mix8(uint8_t fg, uint8_t bg, float a)
{
    return (uint8_t)(fg * a + bg * (1.0f - a) + 0.5f);
}

// Recomputes the whole palette — body, shade, feet, mouth, tongue, edge,
// pupil — at the given desaturation and fade. Called once at init and again
// every frame from creature_update, so low life reads as a fading creature
// rather than a fixed low-life palette swap, and dying fades the whole
// creature toward the background rather than just the skin tones.
static void update_creature_colors(float low_frac, float fade)
{
    float sat = 1.0f - (1.0f - BODY_SAT_MIN) * low_frac;
    C_BODY  = tint_rgb(186, 108, 190, sat, fade);
    C_SHADE = tint_rgb(148, 76, 154, sat, fade);
    // A shade under the underside tone: the feet sit below the body and read
    // as being in its shadow, which also stops them competing with the face.
    C_FOOT  = tint_rgb(132, 66, 138, sat, fade);
    // The mouth and tongue are skin, not ink, so they fade with the rest of
    // the body rather than staying saturated while everything around them
    // goes gray.
    C_MOUTH  = tint_rgb(74, 38, 82, sat, fade);
    C_TONGUE = tint_rgb(205, 145, 200, sat, fade);
    C_EDGE   = tint_rgb(24, 14, 28, 1.0f, fade);
    // 90% white over the eye colour, pre-blended, then faded like everything
    // else.
    const float pupil_alpha = 0.90f;
    C_PUPIL = tint_rgb(mix8(255, 24, pupil_alpha),
                       mix8(255, 14, pupil_alpha),
                       mix8(255, 28, pupil_alpha), 1.0f, fade);
}

void creature_init(void)
{
    C_BG = display_rgb(BG_R, BG_G, BG_B);
    update_creature_colors(0.0f, 0.0f);

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

    c.life = 80.0f;
    c.life_timer = LIFE_TICK_SECONDS;
    c.dead = false;
    c.vanish = 0.0f;
    c.death_timer = 0.0f;
    c.survival_timer = 0.0f;
    c.death_survival_seconds = 0.0f;
}

// Where the body sits on screen this frame. Shared by update and draw so that
// hit-testing a touch and rendering can never disagree about where it is.
static void body_frame(float *cx, float *cy, float *rx, float *ry, float *scale)
{
    // A fixed, slightly distant resting scale. Breathing and touch squash can
    // change the silhouette, but the pet never drifts toward or away from you.
    // Death doesn't shrink or lift this — see the `vanish` field: it falls
    // flat in place, at table height, and disappears there rather than
    // shrinking off into the distance.
    const float s = 0.80f;
    float breath = sinf(c.breathe) * 0.05f;
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

static inline bool point_in_feed_button(int x, int y)
{
    float dx = (float)x - FEED_BTN_CX, dy = (float)y - FEED_BTN_CY;
    return (dx * dx + dy * dy) <= (FEED_BTN_R * FEED_BTN_R);
}

static inline bool point_in_debug_starve_button(int x, int y)
{
    float dx = (float)x - DEBUG_STARVE_BTN_CX, dy = (float)y - DEBUG_STARVE_BTN_CY;
    return (dx * dx + dy * dy) <= (DEBUG_STARVE_BTN_R * DEBUG_STARVE_BTN_R);
}

// A no-op if already dead — a second starve while mid-collapse shouldn't
// restart the freeze or snap the fall back to the beginning.
static void trigger_death(void)
{
    if (c.dead) {
        return;
    }
    c.dead = true;
    c.death_timer = 0.0f;
    // Freeze the lifespan reading right here — survival_timer keeps existing
    // only so the next life has a value to start counting from again.
    c.death_survival_seconds = c.survival_timer;
    // Freeze on a fixed, settled dead face rather than whatever mid-blink or
    // mid-turn pose it happened to be in — the fall reads as one decisive
    // collapse only if it starts from the same expression every time.
    c.eye_open = 1.0f;
    c.gaze_tx = c.gaze_x = 0.0f;
    c.gaze_ty = c.gaze_y = -1.0f;
    // Mouth and smile are left as they were and eased shut gradually in the
    // dead-state update below, rather than snapped here — a slack jaw closes
    // over a beat, it doesn't teleport shut the instant life hits zero.
    c.arousal = 0.0f;
    c.valence = 0.0f;
    c.squash = 0.15f;
    c.squash_velocity = 0.0f;
    c.lean = 0.0f;
}

// Real-time decay: 5-10 points every hour, paused once life bottoms out
// rather than going negative.
static void update_life(float dt)
{
    if (c.life <= 0.0f) {
        c.life = 0.0f;
        return;
    }
    c.life_timer -= dt;
    if (c.life_timer <= 0.0f) {
        float loss = LIFE_DECAY_MIN + gfx_randf() * (LIFE_DECAY_MAX - LIFE_DECAY_MIN);
        c.life = clampf(c.life - loss, 0.0f, 100.0f);
        c.life_timer += LIFE_TICK_SECONDS;
        if (c.life <= 0.0f) {
            trigger_death();
        }
    }
}

static void start_feed(void)
{
    if (c.feed_anim_active) {
        return;   // one pellet in flight at a time
    }
    c.feed_anim_active = true;
    c.feed_t = 0.0f;
    c.feed_chomped = false;
}

// Drives the pellet's flight and, once it lands, the chomp: life rises and a
// happy impulse is fed into the same spring/approach machinery a poke uses, so
// it settles the same way rather than needing its own animation state. A press
// while dead revives the creature instead of triggering a chomp.
static void update_feed_anim(float dt)
{
    if (!c.feed_anim_active) {
        return;
    }
    c.feed_t += dt;

    if (!c.feed_chomped && c.feed_t >= FEED_FLIGHT_TIME) {
        c.feed_chomped = true;
        c.life = clampf(c.life + LIFE_FEED_AMOUNT, 0.0f, 100.0f);

        if (c.dead) {
            if (c.life > 0.0f) {
                c.dead = false;
                c.survival_timer = 0.0f;
                c.eye_open = 1.0f;
                c.arousal = clampf(c.arousal + 0.40f, 0.0f, 1.0f);
                c.valence = clampf(c.valence + 0.30f, 0.0f, 1.0f);
            }
        } else {
            c.squash_velocity += 6.0f;
            c.arousal = clampf(c.arousal + 0.30f, 0.0f, 1.0f);
            c.valence = clampf(c.valence + 0.25f, 0.0f, 1.0f);
            // Snaps open for the bite; the normal per-frame approach() toward
            // mouth_target eases it back down over the following frames.
            c.mouth_open = 0.85f;
        }
    }

    if (c.feed_t >= FEED_ANIM_TOTAL) {
        c.feed_anim_active = false;
        c.feed_chomped = false;
        c.feed_t = 0.0f;
    }
}

void creature_update(float dt, bool touched, int touch_x, int touch_y)
{
    update_life(dt);

    bool on_button = touched && point_in_feed_button(touch_x, touch_y);
    c.button_pressed = on_button;
    if (on_button && !c.was_touched) {
        start_feed();
    }
    update_feed_anim(dt);

    bool on_debug_starve = touched && point_in_debug_starve_button(touch_x, touch_y);
    c.debug_starve_pressed = on_debug_starve;
    if (on_debug_starve && !c.was_touched) {
        c.life = clampf(c.life - DEBUG_STARVE_DECREMENT, 0.0f, 100.0f);
        if (c.life <= 0.0f) {
            trigger_death();
        }
    }

    // Body saturation fades out as life drains below the threshold — see
    // life_low_frac(). low_frac itself only needs live life, so it's read
    // regardless of alive/dead; the fade factor below is what differs.
    float low_frac = life_low_frac(c.life);

    // Starved: frozen on the spot, then presses flat and fades — see the
    // `vanish` field. Only the feed button still does anything.
    if (c.dead) {
        c.death_timer += dt;
        // Held at 0 through the freeze, then eased toward fully gone. Nothing
        // else about the pose updates while dead, so it falls away exactly as
        // frozen rather than continuing to blink or breathe mid-fall — except
        // the mouth, which eases shut over the same beat instead of snapping
        // closed on the frame life hits zero.
        float vanish_target = (c.death_timer > DEATH_FREEZE_SECONDS) ? 1.0f : 0.0f;
        c.vanish = approach(c.vanish, vanish_target, VANISH_RATE, dt);
        c.mouth_open = approach(c.mouth_open, 0.0f, 3.0f, dt);
        c.smile = approach(c.smile, 0.0f, 3.0f, dt);
        update_creature_colors(low_frac, vanish_fade_frac(c.vanish));
        c.tap_pulse -= dt * 3.4f;
        if (c.tap_pulse < 0.0f) { c.tap_pulse = 0.0f; }
        c.was_touched = touched;
        c.was_on_body = false;
        return;
    }

    // Reverses the fall smoothly on revival: the same curve eased back toward
    // 0 rather than a separate un-death animation.
    c.vanish = approach(c.vanish, 0.0f, VANISH_RATE, dt);
    update_creature_colors(low_frac, vanish_fade_frac(c.vanish));

    c.survival_timer += dt;

    // Below LIFE_LOW_THRESHOLD, breathing slows toward barely-there as life
    // approaches zero, rather than cutting off sharply at the death boundary.
    float breath_rate = BREATH_RATE_NORMAL - (BREATH_RATE_NORMAL - BREATH_RATE_BARE) * low_frac;
    c.breathe += dt * breath_rate;
    c.wobble_phase += dt * 0.55f;

    // --- reflex layer: immediate, no decisions ---------------------------
    // Being poked is a jolt first and a feeling second. Gaze has a fast target
    // response; the body carries the physical response through a spring.
    bool on_body = touched && !on_button && !on_debug_starve && creature_contains_point(touch_x, touch_y);
    if (touched && !on_button && !on_debug_starve) {
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
    // Both baselines sag as life runs low, so a hungry creature drifts back
    // toward a duller, sadder resting face instead of the same cheerful
    // neutral regardless of health. The mouth is what actually shows this —
    // see the emotion-to-mouth mapping below — since it carries the
    // creature's expression while the eyes only do gaze and blinking.
    // Health above the low-life threshold still has somewhere to go, so the
    // mood keeps climbing over the top half of the bar rather than sitting
    // pinned. That is what lets half health be the exact point where the mouth
    // is neutral — with the baseline flat above 50 the creature would have to
    // wear the same face from 100 down to 50, and the pivot could only be
    // placed by making full health look no happier than half.
    // Only the mood axes span the full range; breathing and colour still key
    // off low_frac and so stay unchanged above 50.
    float high_frac = clampf((c.life - LIFE_LOW_THRESHOLD)
                             / (100.0f - LIFE_LOW_THRESHOLD), 0.0f, 1.0f);
    c.arousal = approach(c.arousal, 0.22f - low_frac * 0.15f + high_frac * 0.06f, 0.35f, dt);
    c.valence = approach(c.valence, 0.55f - low_frac * 0.45f + high_frac * 0.20f, 0.12f, dt);
    // A lightly underdamped spring replaces a hard squash followed by a
    // mechanical exponential decay. It makes each poke feel soft and alive.
    c.squash_velocity += -54.0f * c.squash * dt;
    c.squash_velocity *= expf(-9.0f * dt);
    c.squash += c.squash_velocity * dt;

    // --- emotion drives the visible parameters ---------------------------
    float mouth_target = 0.20f + c.arousal * 0.55f + c.valence * 0.12f;
    float smile_target = 0.15f + c.valence * 0.85f;
    // The mouth carries the creature's expression, so it follows a feeling
    // rather than jumping to it — but it still has to keep up with the face.
    // A real animal's mouth moves fast; easing this too slowly reads as the
    // expression lagging behind whatever just happened, which is exactly the
    // latency that makes a pet feel like a machine. The jaw outruns the mood
    // it is expressing, so mouth_open eases faster than smile.
    c.mouth_open = approach(c.mouth_open, mouth_target, 36.0f, dt);
    c.smile = approach(c.smile, smile_target, 22.0f, dt);

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
// remains correctly attached when the body breathes or squashes. `anchor_y`/
// `ky` are the death flatten (identity when upright) — applied last, to the
// finished point list, rather than threaded through the generation maths
// above.
static void draw_mouth(float cx, float cy, float s, float anchor_y, float ky)
{
    const float turn = 1.0f - 0.16f * fabsf(c.facing);
    float mx = cx + (c.lean * 0.5f + c.facing * 7.0f) * s;

    // `smile` is the smoothed mood axis, and its measured range in play is
    // roughly 0.28 when starving through 0.63 idle to 0.77 just after a feed.
    // Mapping that onto a signed bend is what makes mood visible at all: it
    // used to drive only the mouth's width, over a 5% span nobody could see,
    // so the creature wore the same grin whether it was delighted or dying.
    // Neutral sits between the starving and healthy readings, so a healthy
    // creature is visibly smiling and a starving one visibly is not.
    // Clamped asymmetrically: a full frown is worth reaching, but the happy
    // end tops out short of the full swing, because the widest grin the maths
    // allows overpowers the rest of the face. Feeding pins the mood axis to
    // its ceiling, so without this cap the creature spends every fed moment
    // at maximum grin and the expression stops meaning anything.
    // The pivot is the resting `smile` at exactly half life, so half a bar of
    // health is the moment the mouth crosses from a smile to a frown. It has
    // to track the valence baseline in creature_update(): resting smile is
    // 0.15 + 0.85 * valence, and valence rests at 0.55 when life is 50.
    const float bend = clampf((c.smile - 0.6175f) * 3.8f, -1.0f, 0.65f);

    const int teeth = 4;
    const int top_steps = teeth * 10;
    const int side_steps = 12;
    const int bowl_steps = 32;
    // Sad mouths are small ones, so width follows the bend too.
    //
    // The widest point is the bowl line, not the top edge: the top stops short
    // by `corner_inset` and the corner curve runs outward and down to meet the
    // bowl. Having it the other way round — a wide top over a narrower bowl —
    // forced the corner to bulge out and then come back in, and that S-turn
    // rendered as a horn jutting out of each corner of the mouth.
    const float mouth_rx = (60.0f + bend * 7.0f) * s * turn;
    const float corner_inset = 7.0f * s;
    const float top_rx = mouth_rx - corner_inset;
    const float bowl_rx = mouth_rx;
    const float top_y = cy - 15.0f * s;
    const float bowl_y = top_y + (17.0f + c.mouth_open * 4.0f) * s;
    // Below this, both the bowl and the teeth scale down toward a flat closed
    // line rather than staying a fixed-depth grin. This threshold must stay
    // well under the lowest mouth_open ever reached by the arousal/valence
    // baseline at 1% health (measured at 0.26) — sitting right at that floor
    // previously made the mouth flicker flat exactly when health was low,
    // instead of only closing for the 0.0 the dead face eases down to.
    const float open_shape = clampf(c.mouth_open / 0.06f, 0.0f, 1.0f);
    // Mood shrinks the mouth as well as bending it. Tilting a mouth that stays
    // this deep just reads as a big mouth on a slant; a miserable creature
    // needs a small one, and that is most of what sells the difference.
    const float mood_open = clampf(0.40f + 0.62f * (bend * 0.5f + 0.5f), 0.0f, 1.0f);
    // Shallower than it is wide, so the bottom reads as a broad straight-ish
    // sweep rather than a deep sagging U. This is the safe way to flatten it —
    // see the bowl loop below for why reshaping the curve itself is not.
    const float bowl_depth = (21.0f + c.mouth_open * 14.0f) * s * open_shape * mood_open;
    // Teeth are a grin's worth of expression on their own: a miserable
    // creature keeps a smooth lip line, and the scallops only come out as the
    // mood turns up. Without this a deep frown still reads as a toothy smile.
    const float tooth_mood = clampf(bend * 0.55f + 0.62f, 0.0f, 1.0f);
    const float tooth_depth = 8.5f * s * open_shape * tooth_mood;
    const float sag = (3.0f + c.smile * 2.0f) * s;
    // Kept under corner_inset so the corner curve's control hull cannot push
    // it back outside the bowl's width — that overshoot is the horn.
    const float corner_dx = 5.0f * s;
    const float side_tangent = 12.0f * s;
    // How far the corners travel between a full frown and a full grin.
    const float corner_lift = bend * 21.0f * s;

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

    // One uninterrupted bowl, right to left.
    //
    // The depth profile must leave a=0 with a finite slope. Anything that
    // approaches a vertical tangent there — sin(a) raised to a power below 1,
    // for instance — makes the first segment almost purely vertical, and an
    // outline built by offsetting along edge normals has nothing stable to
    // work from on a segment that short and that steep: the outline drops out
    // and the fill tears in half. Flattening is done by shortening the bowl,
    // not by reshaping this curve.
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

    for (int i = 0; i < n; i++) {
        gfx_pt_t b = bend_pt(p[i].x, p[i].y, mx, mouth_rx, corner_lift);
        p[i] = flat_pt(b.x, b.y, anchor_y, ky);
    }
    gfx_fill_poly_outlined(p, n, C_MOUTH, C_EDGE, 2.0f * s);

    if (c.mouth_open > 0.32f) {
        float t = (c.mouth_open - 0.32f) / 0.68f;

        // The tongue is sized by how open the mouth is, but the bowl holding
        // it also shrinks with mood, so the two are not in fixed proportion:
        // an unclamped tongue punches through the bottom lip on a small,
        // unhappy mouth. Fit it to the room actually available instead.
        const float offset_x = 4.0f * s;
        const float margin = 3.0f * s;            // clears the mouth's outline
        const float ty_off = bowl_depth * 0.55f;  // below the bowl's top line

        float rx = (26.0f * t + 10.0f) * s * turn;
        float ry = (15.0f * t + 5.0f) * s;

        // A smile bends the bowl's sides upward but the tongue is a plain
        // ellipse that does not bend with it, so the clearance it loses at its
        // own edges has to come out of the budget. Computed from the unclamped
        // rx, which only ever overstates the loss.
        float u_edge = clampf((offset_x + rx) / mouth_rx, 0.0f, 1.0f);
        float bend_loss = (corner_lift > 0.0f) ? corner_lift * u_edge * u_edge : 0.0f;

        float ry_room = bowl_depth - ty_off - margin - bend_loss;
        if (ry > ry_room) { ry = ry_room; }

        // Half-width the bowl still has at the tongue's lowest point — the
        // ellipse narrows as it deepens, so this is what actually bounds rx.
        float deep = clampf((ty_off + ry) / bowl_depth, 0.0f, 1.0f);
        float rx_room = bowl_rx * sqrtf(1.0f - deep * deep) - margin - offset_x;
        if (rx > rx_room) { rx = rx_room; }

        if (rx > 1.0f && ry > 1.0f) {
            gfx_pt_t tb = bend_pt(mx - offset_x, bowl_y + ty_off,
                                  mx, mouth_rx, corner_lift);
            gfx_pt_t tongue = flat_pt(tb.x, tb.y, anchor_y, ky);
            // ky scales the tongue exactly as flat_pt scales the mouth around
            // the same anchor, so a fit proven here survives the flatten.
            gfx_fill_ellipse(tongue.x, tongue.y, rx, ry * ky, C_TONGUE);
        }
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

// The feed button: a bowl of kibble, outside the pet fiction like the rotary
// encoder in AGENTS.md. Two circles for the outline, same trick the rest of
// the creature uses for outlines, valid here because a circle is convex.
static void draw_feed_button(void)
{
    float r = c.button_pressed ? FEED_BTN_R * 0.88f : FEED_BTN_R;
    gfx_fill_ellipse(FEED_BTN_CX, FEED_BTN_CY, r, r, C_EDGE);
    gfx_fill_ellipse(FEED_BTN_CX, FEED_BTN_CY, r - 3.0f, r - 3.0f, display_rgb(255, 205, 120));

    const uint16_t kibble = display_rgb(150, 92, 40);
    const float dot_r = r * 0.16f;
    for (int i = 0; i < 3; i++) {
        float a = -PI / 2.0f + (float)i * (2.0f * PI / 3.0f);
        gfx_fill_ellipse(FEED_BTN_CX + cosf(a) * r * 0.36f,
                         FEED_BTN_CY + sinf(a) * r * 0.36f,
                         dot_r, dot_r, kibble);
    }
}

// The pellet's flight only, arcing from the button to roughly where the mouth
// is. The chomp itself is not drawn separately — it is the mouth_open spike in
// update_feed_anim(), read back through the normal mouth drawing.
static void draw_feed_pellet(float s)
{
    if (!c.feed_anim_active || c.feed_t >= FEED_FLIGHT_TIME) {
        return;
    }
    float cx, cy, rx, ry, bs;
    body_frame(&cx, &cy, &rx, &ry, &bs);

    float t = c.feed_t / FEED_FLIGHT_TIME;

    // Aim for the centre of the mouth rather than the upper-left edge of the
    // body. Keep the horizontal turn offset in step with draw_mouth(), so the
    // cookie still lands centrally when the creature is looking aside.
    float x1 = cx + (c.lean * 0.5f + c.facing * 7.0f) * s;
    float y1 = cy + 4.0f * s;
    float x = FEED_BTN_CX + (x1 - FEED_BTN_CX) * t;
    float y = FEED_BTN_CY + (y1 - FEED_BTN_CY) * t - sinf(t * PI) * 26.0f;

    gfx_fill_ellipse(x, y, 5.0f * s, 5.0f * s, C_EDGE);
    gfx_fill_ellipse(x, y, 3.5f * s, 3.5f * s, display_rgb(255, 205, 120));
}

// A minimal 3x5 bitmap font, just enough to show the life number. Nothing
// else on the creature needs text, so this stays local rather than becoming a
// shared module.
// Indices 10-12 are the H/M/S unit letters the survival readout needs;
// everything else in the creature only ever needed digits.
#define FONT_H 10
#define FONT_M 11
#define FONT_S 12
static const uint8_t FONT3X5[13][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},   // 0
    {0x2, 0x6, 0x2, 0x2, 0x7},   // 1
    {0x7, 0x1, 0x7, 0x4, 0x7},   // 2
    {0x7, 0x1, 0x7, 0x1, 0x7},   // 3
    {0x5, 0x5, 0x7, 0x1, 0x1},   // 4
    {0x7, 0x4, 0x7, 0x1, 0x7},   // 5
    {0x7, 0x4, 0x7, 0x5, 0x7},   // 6
    {0x7, 0x1, 0x1, 0x1, 0x1},   // 7
    {0x7, 0x5, 0x7, 0x5, 0x7},   // 8
    {0x7, 0x5, 0x7, 0x1, 0x7},   // 9
    {0x5, 0x5, 0x7, 0x5, 0x5},   // H
    {0x5, 0x7, 0x5, 0x5, 0x5},   // M
    {0x7, 0x4, 0x7, 0x1, 0x7},   // S
};

static void draw_digit(float x, float y, int digit, float cell, uint16_t colour)
{
    for (int row = 0; row < 5; row++) {
        uint8_t bits = FONT3X5[digit][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (0x4 >> col)) {
                display_fill_rect((int)(x + (float)col * cell), (int)(y + (float)row * cell),
                                  (int)cell, (int)cell, colour);
            }
        }
    }
}

// Right-aligns so the number reads naturally against the bar beside it,
// without pulling in a string library for three-digit integer formatting.
static void draw_number_right_aligned(float right_x, float y, int value, float cell, uint16_t colour)
{
    if (value < 0) { value = 0; }
    if (value > 100) { value = 100; }

    int digits[3], n = 0;
    int hundreds = value / 100, tens = (value / 10) % 10, ones = value % 10;
    if (hundreds > 0) { digits[n++] = hundreds; }
    if (n > 0 || tens > 0) { digits[n++] = tens; }
    digits[n++] = ones;

    float digit_w = 3.0f * cell, gap = cell;
    float x = right_x - ((float)n * digit_w + (float)(n - 1) * gap);
    for (int i = 0; i < n; i++) {
        draw_digit(x, y, digits[i], cell, colour);
        x += digit_w + gap;
    }
}

static void draw_life_ui(void)
{
    const float x = LIFE_BAR_X, y = LIFE_BAR_Y, w = LIFE_BAR_W, h = LIFE_BAR_H;

    display_fill_rect((int)(x - 2), (int)(y - 2), (int)(w + 4), (int)(h + 4), C_EDGE);
    display_fill_rect((int)x, (int)y, (int)w, (int)h, display_rgb(235, 230, 225));

    float frac = clampf(c.life / 100.0f, 0.0f, 1.0f);
    float fill_w = w * frac;
    uint16_t fill_colour = (c.life > 50.0f) ? display_rgb(96, 180, 90)
                          : (c.life > 20.0f) ? display_rgb(230, 175, 60)
                                             : display_rgb(205, 70, 60);
    if (fill_w > 0.5f) {
        display_fill_rect((int)x, (int)y, (int)fill_w, (int)h, fill_colour);
    }

    draw_number_right_aligned(x - 6.0f, y + 1.0f, (int)(c.life + 0.5f), 2.0f, C_EDGE);
}

// How long this life lasted, in the same tiny digit font as the life bar
// number, plus H/M/S unit letters: seconds under a minute, minutes under an
// hour, hours and minutes beyond that. Shown for the whole dead state rather
// than only once it has faded, and drawn in a fixed ink colour independent of
// the creature's own fade-out so it stays legible throughout.
static void draw_survival_message(void)
{
    if (!c.dead) {
        return;
    }

    int total_sec = (int)c.death_survival_seconds;
    int total_min = total_sec / 60;
    int codes[6];
    int nc = 0;
    if (total_sec < 60) {
        if (total_sec >= 10) { codes[nc++] = total_sec / 10; }
        codes[nc++] = total_sec % 10;
        codes[nc++] = FONT_S;
    } else if (total_min < 60) {
        if (total_min >= 10) { codes[nc++] = total_min / 10; }
        codes[nc++] = total_min % 10;
        codes[nc++] = FONT_M;
    } else {
        int hours = total_min / 60;
        int rem_min = total_min % 60;
        if (hours >= 10) { codes[nc++] = hours / 10; }
        codes[nc++] = hours % 10;
        codes[nc++] = FONT_H;
        codes[nc++] = rem_min / 10;
        codes[nc++] = rem_min % 10;
        codes[nc++] = FONT_M;
    }

    // The unit letters are drawn at half the digits' cell size so the numbers
    // stay the thing being read and H/M/S sit beside them as annotation. Half
    // rather than some fraction in between because draw_digit truncates its
    // cell to whole pixels, and a cell that lands on a fraction leaves gaps
    // between the squares making up each glyph.
    const float cell = 4.0f;
    const float unit_cell = cell * 0.5f;
    const float gap = cell;

    // Widths are summed rather than multiplied out: the glyphs are no longer
    // all the same width, so the centring has to account for which is which.
    float total_w = 0.0f;
    for (int i = 0; i < nc; i++) {
        total_w += 3.0f * (codes[i] >= FONT_H ? unit_cell : cell);
        if (i > 0) { total_w += gap; }
    }

    float x = (float)DISPLAY_WIDTH / 2.0f - total_w / 2.0f;
    const float y = 70.0f;
    const uint16_t colour = display_rgb(24, 14, 28);
    for (int i = 0; i < nc; i++) {
        float gc = (codes[i] >= FONT_H) ? unit_cell : cell;
        // Every glyph is 5 cells tall, so offsetting by the difference sits
        // the small letters on the digits' baseline instead of their top.
        draw_digit(x, y + 5.0f * (cell - gc), codes[i], gc, colour);
        x += 3.0f * gc + gap;
    }
}

// Debug aid only — see the constant block above.
static void draw_debug_starve_button(void)
{
    float r = c.debug_starve_pressed ? DEBUG_STARVE_BTN_R * 0.85f : DEBUG_STARVE_BTN_R;
    gfx_fill_ellipse(DEBUG_STARVE_BTN_CX, DEBUG_STARVE_BTN_CY, r, r, C_EDGE);
    gfx_fill_ellipse(DEBUG_STARVE_BTN_CX, DEBUG_STARVE_BTN_CY, r - 2.5f, r - 2.5f,
                     display_rgb(200, 60, 60));
    draw_digit(DEBUG_STARVE_BTN_CX - 3.0f, DEBUG_STARVE_BTN_CY - 5.0f, 0, 2.0f,
               display_rgb(255, 255, 255));
}

void creature_draw(void)
{
    display_fill(C_BG);

    // One fixed scale carries the whole creature. Breathing scales the body very
    // slightly; squash trades height for width so volume looks conserved when it
    // reacts.
    float cx, cy, rx, ry, s;
    body_frame(&cx, &cy, &rx, &ry, &s);

    // The whole silhouette presses down flat onto the ground over the full
    // vanish range (see the `vanish` field) — a vertical scale about a fixed
    // anchor, not a rotation, like paper falling flat rather than toppling.
    // At table height a paper-thin sheet has no cross-section left to see, so
    // this alone is what makes it disappear; colour only fades afterward, in
    // vanish_fade_frac(), once it's already flat.
    float ky = 1.0f - c.vanish;
    float anchor_y = cy + ry * 0.9f;

    // The tail sweeps to the side opposite `facing`, passing behind the body at
    // the midpoint. Thinning it as it crosses sells the Y turn rather than a
    // teleport, since an edge-on tail should almost vanish.
    {
        float f = c.facing;
        float w = 19.0f * s * (0.30f + 0.70f * fabsf(f)) * ky;
        gfx_pt_t base = flat_pt(cx - f * 58.0f * s + c.lean * 0.6f, cy - 30.0f * s, anchor_y, ky);
        gfx_pt_t tip = flat_pt(cx - f * 96.0f * s + c.lean * 1.4f, cy - 74.0f * s, anchor_y, ky);
        // The regular tail keeps its outline. A narrow, unoutlined blade laid
        // into the lower edge is the shadow: roughly the lower quarter only.
        // Width and curve both scale with ky, so it collapses flat along with
        // the rest of the silhouette instead of staying a fixed-width bar.
        draw_blade(base.x, base.y, tip.x, tip.y, w, f * 7.0f * ky, C_BODY, true, 0.0f);
        // The shadow blade is one quarter as wide, so biasing its centre by
        // three of its own widths places it over the outer lower quarter of
        // the full tail rather than leaving it centred on the spine.
        draw_blade(base.x, base.y, tip.x, tip.y, w * 0.25f, f * 7.0f * ky, C_SHADE, false, 3.0f);
    }

    // Feet shift a little with the turn so the body does not look bolted down.
    float fx = c.facing * 6.0f * s;
    {
        gfx_pt_t lb = flat_pt(cx - 36.0f * s + fx + c.lean * 0.3f, cy + ry * 0.74f, anchor_y, ky);
        gfx_pt_t lt = flat_pt(cx - 62.0f * s + fx, cy + ry * 1.00f, anchor_y, ky);
        gfx_pt_t rb = flat_pt(cx + 36.0f * s + fx + c.lean * 0.3f, cy + ry * 0.74f, anchor_y, ky);
        gfx_pt_t rt = flat_pt(cx + 64.0f * s + fx, cy + ry * 0.98f, anchor_y, ky);
        draw_blade(lb.x, lb.y, lt.x, lt.y, 17.0f * s * ky, 7.0f * ky, C_FOOT, true, 0.0f);
        draw_blade(rb.x, rb.y, rt.x, rt.y, 17.0f * s * ky, -7.0f * ky, C_FOOT, true, 0.0f);
    }

    const int BODY_PTS = 56;
    static gfx_pt_t body[GFX_MAX_POLY_PTS];
    int n = gfx_blob_points(body, BODY_PTS, cx, cy, rx, ry,
                            c.lean, 0.018f, c.wobble_phase, BODY_TAPER);

    // Underside shadow: the body's lower arc, closed off by the same arc pulled
    // inward toward the centre. Both edges follow the silhouette, so the shadow
    // curves along the body and stays symmetric — an offset second blob would
    // instead skew the boundary in whatever direction it was offset. Computed
    // from the unflattened silhouette so the inward pull reads correctly; both
    // polygons are flattened together afterward.
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

    for (int i = 0; i < n; i++) {
        body[i] = flat_pt(body[i].x, body[i].y, anchor_y, ky);
    }
    for (int i = 0; i < m; i++) {
        sh[i] = flat_pt(sh[i].x, sh[i].y, anchor_y, ky);
    }
    gfx_fill_poly_outlined(body, n, C_BODY, C_EDGE, 3.0f * s);
    gfx_fill_poly(sh, m, C_SHADE);

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
        float erx = 7.6f * s * squeeze, ery = 10.5f * s * c.eye_open * ky;
        gfx_pt_t eye = flat_pt(ex, ey + gy, anchor_y, ky);
        gfx_fill_ellipse(eye.x, eye.y, erx, ery, C_EDGE);

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
            gfx_pt_t pupil = flat_pt(ex + dx, ey + gy + dy, anchor_y, ky);
            gfx_fill_ellipse(pupil.x, pupil.y, prx, pry, C_PUPIL);
        }
    }

    draw_mouth(cx, cy, s, anchor_y, ky);
    draw_tap_pulse(s);

    draw_feed_button();
    draw_feed_pellet(s);
    draw_life_ui();
    draw_debug_starve_button();
    draw_survival_message();
}
