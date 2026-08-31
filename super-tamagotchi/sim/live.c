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
//
// Deliberately matched to the board rather than to what a desktop can do:
//
//   * Frame rate is paced by serve.py to the board's ~31fps, not the host's.
//   * The frame loop runs on a thread whose stack is measured against the
//     device's CONFIG_ESP_MAIN_TASK_STACK_SIZE. A desktop process has megabytes,
//     which is why a stack overflow that rebooted the board every twenty seconds
//     ran perfectly well here. Now it is reported instead of hidden.
//   * Touch is passed through directly, matching the current factory-calibrated
//     capacitive panel.

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "canvas.h"
#include "creature.h"

// Must track CONFIG_ESP_MAIN_TASK_STACK_SIZE in sdkconfig.defaults.
#define BOARD_MAIN_STACK 8192

// Room to actually overflow the budget without faulting, so the overrun can be
// measured and reported rather than crashing the emulator.
#define PROBE_STACK (512 * 1024)
#define FILL_BYTE   0xA5

static uint8_t *stack_mem;

// Stacks grow downward, so the deepest point reached is the lowest address that
// no longer holds the fill pattern. Same idea as FreeRTOS's high-water mark.
static size_t stack_used(void)
{
    size_t i = 0;
    while (i < PROBE_STACK && stack_mem[i] == FILL_BYTE) {
        i++;
    }
    return PROBE_STACK - i;
}

static void *frame_loop(void *unused)
{
    (void)unused;

    uint16_t *fb = calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, sizeof(uint16_t));
    if (!fb) {
        return NULL;
    }
    canvas_set_framebuffer(fb);
    creature_init();

    size_t peak = 0;
    bool warned = false;
    char line[128];

    while (fgets(line, sizeof(line), stdin)) {
        float dt = 0.0f;
        int touched = 0, x = 0, y = 0;
        if (sscanf(line, "%f %d %d %d", &dt, &touched, &x, &y) != 4) {
            continue;
        }
        // Clamp the step exactly as the firmware does, so a stalled browser tab
        // cannot lurch the animation in a way the board never would.
        if (dt < 0.0f) { dt = 0.0f; }
        if (dt > 0.1f) { dt = 0.1f; }

        creature_update(dt, touched != 0, x, y);
        creature_draw();

        fwrite(fb, sizeof(uint16_t), DISPLAY_WIDTH * DISPLAY_HEIGHT, stdout);
        fflush(stdout);

        size_t used = stack_used();
        if (used > peak) {
            peak = used;
            if (peak > BOARD_MAIN_STACK && !warned) {
                warned = true;
                fprintf(stderr,
                        "\n*** STACK BUDGET EXCEEDED: %zu bytes used, board has "
                        "%d ***\n*** this would overflow and reboot the device "
                        "***\n\n", peak, BOARD_MAIN_STACK);
            }
        }
    }

    fprintf(stderr, "peak stack: %zu of %d bytes (%zu%% of the board's budget)\n",
            peak, BOARD_MAIN_STACK, peak * 100 / BOARD_MAIN_STACK);
    free(fb);
    return NULL;
}

int main(void)
{
    // pthread_attr_setstack wants page-aligned memory on macOS.
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    if (posix_memalign((void **)&stack_mem, page, PROBE_STACK) != 0) {
        return 1;
    }
    memset(stack_mem, FILL_BYTE, PROBE_STACK);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack_mem, PROBE_STACK);

    pthread_t tid;
    if (pthread_create(&tid, &attr, frame_loop, NULL) != 0) {
        // Fall back to the default stack — loses the budget check, keeps the
        // emulator working.
        fprintf(stderr, "warning: could not set a probe stack; "
                        "stack budget will not be checked\n");
        frame_loop(NULL);
        return 0;
    }
    pthread_join(tid, NULL);
    free(stack_mem);
    return 0;
}
