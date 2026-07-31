// The creature: a purple blob with a huge toothy grin.
//
// Drawn entirely from parameters, never from stored frames. Emotion is not an
// animation to play back — it *is* the parameter set, so expression blends
// continuously and no two moments look identical.
//
// Emotion is two continuous axes rather than a list of moods. A mood list is a
// state machine, and people feel the switching; valence/arousal blends instead.
//
// Note this design puts expression in the MOUTH, not the eyes. The reference art
// has tiny dot eyes and an enormous grin, so the mouth is the primary emotional
// channel and the eyes only carry gaze and blinking.

#pragma once

#include <stdbool.h>

void creature_init(void);

// Advances the simulation. `dt` in seconds. Pass the touch point in display
// coordinates, or touched=false when the panel is idle.
void creature_update(float dt, bool touched, int touch_x, int touch_y);

// Renders the current state into the framebuffer. Does not flush.
void creature_draw(void);
