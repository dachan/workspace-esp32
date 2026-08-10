#pragma once

#include <stdbool.h>

#include "esp_err.h"

// MAX98357 output. The amp is left enabled rather than shut down between sounds:
// toggling SD costs tON plus a clock re-lock that a short sound cannot outlast.
// Call first thing at boot: the amp powers itself on from the JTAG pins' reset
// pull-ups and amplifies floating-input noise until this runs.
esp_err_t audio_early_mute(void);
esp_err_t audio_init(void);
// Short ascending chime at boot.
void audio_play_startup(void);
// Touch feedback, rendered on the audio task so the render loop never blocks.
void audio_play_touch(bool on_creature);
// Bring-up diagnostic: 1kHz at three rising levels. Not called at boot — the
// quiet step surviving while the loud one breaks up indicates a resistive
// supply connection rather than a broken signal.
void audio_selftest(void);
