// Desktop renderer for the creature.
//
// Compiles the *same* creature.c and gfx.c the firmware uses, against a malloc'd
// framebuffer instead of PSRAM. Two outputs:
//
//   sim/out.bmp   six curated stills, for inspecting shapes frame by frame
//   sim/film.bmp  a grid of consecutive frames, turned into a looping animation
//                 by sim/make_page.py so the motion can be judged in a browser
//
// Motion is the point: breathing, blink timing, saccades and the recoil from a
// poke are all things a still cannot show. Iterating here takes a second, where
// a flash cycle takes a minute and a half.
//
// Deliberately no SDL or emscripten — a BMP writer is thirty lines and needs
// nothing installed.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "creature.h"

#define FILM_FPS    30
#define FILM_COLS   10
#define FILM_ROWS   12
#define FILM_FRAMES (FILM_COLS * FILM_ROWS)

static uint8_t *sheet;
static int sheet_w, sheet_h;

static void sheet_alloc(int cols, int rows)
{
    sheet_w = DISPLAY_WIDTH * cols;
    sheet_h = DISPLAY_HEIGHT * rows;
    sheet = calloc((size_t)sheet_w * sheet_h * 3, 1);
    if (!sheet) {
        fprintf(stderr, "out of memory for %dx%d sheet\n", sheet_w, sheet_h);
        exit(1);
    }
}

static void copy_frame(const uint16_t *fb, int col, int row)
{
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        uint8_t *dst = sheet + (((size_t)(row * DISPLAY_HEIGHT + y) * sheet_w)
                                + (size_t)col * DISPLAY_WIDTH) * 3;
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            // The framebuffer holds byte-swapped RGB565 because that is what the
            // ILI9341 wants; undo it to get back to plain colour channels.
            uint16_t v = fb[y * DISPLAY_WIDTH + x];
            uint16_t c = (uint16_t)((v >> 8) | (v << 8));
            dst[x * 3 + 0] = (uint8_t)(((c >> 11) & 0x1F) << 3);
            dst[x * 3 + 1] = (uint8_t)(((c >> 5) & 0x3F) << 2);
            dst[x * 3 + 2] = (uint8_t)((c & 0x1F) << 3);
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
    const int row_bytes = sheet_w * 3;
    const int pad = (4 - (row_bytes % 4)) % 4;
    const int data = (row_bytes + pad) * sheet_h;

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(uint32_t *)(hdr + 2) = 54 + data;
    *(uint32_t *)(hdr + 10) = 54;
    *(uint32_t *)(hdr + 14) = 40;
    *(int32_t *)(hdr + 18) = sheet_w;
    *(int32_t *)(hdr + 22) = -sheet_h;   // negative height = top-down rows
    *(uint16_t *)(hdr + 26) = 1;
    *(uint16_t *)(hdr + 28) = 24;
    *(uint32_t *)(hdr + 34) = data;
    fwrite(hdr, 1, 54, f);

    uint8_t zero[3] = {0, 0, 0};
    for (int y = 0; y < sheet_h; y++) {
        const uint8_t *src = sheet + (size_t)y * sheet_w * 3;
        for (int x = 0; x < sheet_w; x++) {
            uint8_t bgr[3] = {src[x * 3 + 2], src[x * 3 + 1], src[x * 3 + 0]};
            fwrite(bgr, 1, 3, f);
        }
        if (pad) {
            fwrite(zero, 1, pad, f);
        }
    }
    fclose(f);
}

static void advance(float seconds, bool touched, int tx, int ty)
{
    const float step = 1.0f / (float)FILM_FPS;
    for (float t = 0; t < seconds; t += step) {
        creature_update(step, touched, tx, ty);
    }
}

// Six curated moments — settled, mid-poke, recovering — for judging shapes.
static void render_stills(uint16_t *fb)
{
    sheet_alloc(3, 2);
    creature_init();

    advance(1.0f, false, 0, 0);   creature_draw(); copy_frame(fb, 0, 0);
    advance(0.20f, true, 240, 90); creature_draw(); copy_frame(fb, 1, 0);
    advance(0.60f, false, 0, 0);  creature_draw(); copy_frame(fb, 2, 0);
    advance(2.0f, false, 0, 0);   creature_draw(); copy_frame(fb, 0, 1);
    advance(0.20f, true, 80, 190); creature_draw(); copy_frame(fb, 1, 1);
    advance(3.0f, false, 0, 0);   creature_draw(); copy_frame(fb, 2, 1);

    write_bmp("sim/out.bmp");
    free(sheet);
}

// A continuous run with scripted pokes, laid out as a grid of consecutive
// frames. make_page.py turns it into a looping animation.
static void render_film(uint16_t *fb)
{
    sheet_alloc(FILM_COLS, FILM_ROWS);
    creature_init();
    advance(2.0f, false, 0, 0);   // let it settle before recording

    const float step = 1.0f / (float)FILM_FPS;
    for (int i = 0; i < FILM_FRAMES; i++) {
        float t = (float)i * step;

        // Two pokes, in different places, far enough apart to see the full
        // recoil and the slow drift back to baseline.
        bool touched = false;
        int tx = 0, ty = 0;
        if (t > 1.0f && t < 1.18f)  { touched = true; tx = 245; ty = 95; }
        if (t > 2.40f && t < 2.58f) { touched = true; tx = 80;  ty = 185; }

        creature_update(step, touched, tx, ty);
        creature_draw();
        copy_frame(fb, i % FILM_COLS, i / FILM_COLS);
    }

    write_bmp("sim/film.bmp");
    printf("%d %d %d %d\n", FILM_COLS, FILM_ROWS, FILM_FRAMES, FILM_FPS);
    free(sheet);
}

int main(void)
{
    uint16_t *fb = calloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, sizeof(uint16_t));
    canvas_set_framebuffer(fb);

    render_stills(fb);
    render_film(fb);

    free(fb);
    return 0;
}
