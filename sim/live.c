// Live creature process for the interactive emulator.
//
// Reads one line of input state per frame on stdin and writes one raw
// framebuffer on stdout. sim/serve.py drives it from the browser, so the mouse
// becomes the touch panel and the real creature_update/creature_draw run in
// real time — no prerendering, no WebAssembly toolchain, no SDL.
//
// Protocol, one line in / one frame out:
//   in : "<dt seconds> <touched 0|1> <x> <y>\n"
//   out: DISPLAY_WIDTH * DISPLAY_HEIGHT uint16 RGB565, byte-swapped exactly as
//        the panel receives them

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "canvas.h"
#include "creature.h"

int main(void)
{
    uint16_t *fb = calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, sizeof(uint16_t));
    if (!fb) {
        return 1;
    }
    canvas_set_framebuffer(fb);
    creature_init();

    char line[128];
    while (fgets(line, sizeof(line), stdin)) {
        float dt = 0.0f;
        int touched = 0, x = 0, y = 0;
        if (sscanf(line, "%f %d %d %d", &dt, &touched, &x, &y) != 4) {
            continue;
        }
        // Clamp the step so a stalled browser tab cannot lurch the animation.
        if (dt < 0.0f)  { dt = 0.0f; }
        if (dt > 0.1f)  { dt = 0.1f; }

        creature_update(dt, touched != 0, x, y);
        creature_draw();

        fwrite(fb, sizeof(uint16_t), DISPLAY_WIDTH * DISPLAY_HEIGHT, stdout);
        fflush(stdout);
    }

    free(fb);
    return 0;
}
