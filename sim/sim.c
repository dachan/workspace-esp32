// Desktop renderer for the creature.
//
// Compiles the *same* creature.c and gfx.c the firmware uses, against a malloc'd
// framebuffer instead of PSRAM, and writes a contact sheet of frames to a BMP.
// The point is to iterate on the artwork and the animation without a flash cycle
// — and to let whoever is editing the code actually look at the result.
//
// Deliberately no SDL or other dependency: a BMP writer is thirty lines and
// works on any machine with a compiler.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "creature.h"

#define COLS 3
#define ROWS 2
#define SHEET_W (DISPLAY_WIDTH * COLS)
#define SHEET_H (DISPLAY_HEIGHT * ROWS)

static uint8_t sheet[SHEET_H][SHEET_W][3];

static void copy_frame(const uint16_t *fb, int col, int row)
{
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            // The framebuffer holds byte-swapped RGB565 because that is what the
            // ILI9341 wants; undo it to get back to plain colour channels.
            uint16_t v = fb[y * DISPLAY_WIDTH + x];
            uint16_t c = (uint16_t)((v >> 8) | (v << 8));
            uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
            uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
            uint8_t b = (uint8_t)((c & 0x1F) << 3);
            sheet[row * DISPLAY_HEIGHT + y][col * DISPLAY_WIDTH + x][0] = r;
            sheet[row * DISPLAY_HEIGHT + y][col * DISPLAY_WIDTH + x][1] = g;
            sheet[row * DISPLAY_HEIGHT + y][col * DISPLAY_WIDTH + x][2] = b;
        }
    }
}

static void write_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    const int row_bytes = SHEET_W * 3;
    const int pad = (4 - (row_bytes % 4)) % 4;
    const int data = (row_bytes + pad) * SHEET_H;

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(uint32_t *)(hdr + 2) = 54 + data;
    *(uint32_t *)(hdr + 10) = 54;
    *(uint32_t *)(hdr + 14) = 40;
    *(int32_t *)(hdr + 18) = SHEET_W;
    *(int32_t *)(hdr + 22) = -SHEET_H;   // negative height = top-down rows
    *(uint16_t *)(hdr + 26) = 1;
    *(uint16_t *)(hdr + 28) = 24;
    *(uint32_t *)(hdr + 34) = data;
    fwrite(hdr, 1, 54, f);

    uint8_t zero[3] = {0, 0, 0};
    for (int y = 0; y < SHEET_H; y++) {
        for (int x = 0; x < SHEET_W; x++) {
            uint8_t bgr[3] = {sheet[y][x][2], sheet[y][x][1], sheet[y][x][0]};
            fwrite(bgr, 1, 3, f);
        }
        if (pad) {
            fwrite(zero, 1, pad, f);
        }
    }
    fclose(f);
}

// Advances the simulation to `seconds`, at a fixed step, then captures.
static void render_at(uint16_t *fb, float seconds, bool touched, int tx, int ty,
                      int col, int row)
{
    const float step = 1.0f / 30.0f;
    for (float t = 0; t < seconds; t += step) {
        // Only apply the touch over the last stretch, so the poke is fresh when
        // the frame is captured rather than already decayed.
        bool now = touched && (t > seconds - 0.25f);
        creature_update(step, now, tx, ty);
    }
    creature_draw();
    copy_frame(fb, col, row);
}

int main(void)
{
    uint16_t *fb = calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, sizeof(uint16_t));
    canvas_set_framebuffer(fb);
    creature_init();

    // Six moments: settled idle, three points through a poke, and two later
    // frames so breathing and blinking show up at different phases.
    render_at(fb, 1.0f,  false, 0, 0,      0, 0);
    render_at(fb, 0.30f, true, 240,  90,   1, 0);
    render_at(fb, 0.60f, false, 0, 0,      2, 0);
    render_at(fb, 2.0f,  false, 0, 0,      0, 1);
    render_at(fb, 0.30f, true,  80, 190,   1, 1);
    render_at(fb, 3.0f,  false, 0, 0,      2, 1);

    write_bmp("sim/out.bmp");
    printf("wrote sim/out.bmp (%dx%d, %d frames)\n", SHEET_W, SHEET_H, COLS * ROWS);
    free(fb);
    return 0;
}
