#pragma once

#include <stdbool.h>

#include "esp_err.h"

// MAX98357 output. The amp is hard-muted between sounds, and rendering happens
// on a separate task so the display loop never waits for I2S DMA.
esp_err_t audio_init(void);
void audio_play_touch(bool on_creature);
