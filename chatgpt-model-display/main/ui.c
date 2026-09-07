#include "ui.h"

#include <string.h>

#include "build_number.h"
#include "catalog.h"
#include "display.h"
#include "font.h"

static void draw_wrapped(int x, int y, int max_w, const char *text, uint16_t fg, uint16_t bg, int scale)
{
    // Word-wrap on spaces; canvas clipping handles overlong tokens.
    char word[MODEL_PARSE_MAX];
    int cx = x;
    int cy = y;
    int line_h = 8 * scale;
    const char *p = text ? text : "";

    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        size_t wi = 0;
        while (p[wi] && p[wi] != ' ' && wi + 1 < sizeof(word)) {
            word[wi] = p[wi];
            wi++;
        }
        word[wi] = '\0';
        p += wi;

        int ww = font_text_width(word, scale);
        if (cx > x && cx + ww > x + max_w) {
            cx = x;
            cy += line_h + scale;
        }
        // If a single word is still too wide, draw it anyway (may clip).
        font_draw_text(cx, cy, word, fg, bg, scale);
        cx += ww + 6 * scale;  // space width approx
        if (cx - 6 * scale > x + max_w) {
            cx = x;
            cy += line_h + scale;
        }
    }
}


static void draw_thinking_bar(int x, int y, int w, int h, int level, int max_level,
                              uint16_t track, uint16_t fill)
{
    /* Segmented meter (ChatGPT-style): discrete pills with gaps. */
    if (max_level < 1) {
        max_level = 1;
    }
    if (level < 0) {
        level = 0;
    }
    if (level > max_level) {
        level = max_level;
    }

    const int gap = 4;
    const int segs = max_level;
    const int total_gap = gap * (segs - 1);
    int seg_w = (w - total_gap) / segs;
    if (seg_w < 2) {
        seg_w = 2;
    }
    /* Re-center leftover pixels into the last segment. */
    int used = seg_w * segs + total_gap;
    int leftover = w - used;

    int cx = x;
    for (int i = 0; i < segs; i++) {
        int sw = seg_w + (i == segs - 1 ? leftover : 0);
        uint16_t col = (i < level) ? fill : track;
        display_fill_rect(cx, y, sw, h, col);
        cx += sw + gap;
    }
}

esp_err_t ui_render(const model_fields_t *fields)
{
    const uint16_t bg = display_rgb(12, 14, 22);
    const uint16_t card = display_rgb(24, 28, 42);
    const uint16_t accent = display_rgb(88, 166, 255);
    const uint16_t label = display_rgb(140, 150, 170);
    const uint16_t text = display_rgb(240, 244, 250);
    const uint16_t muted = display_rgb(110, 118, 135);

    display_fill(bg);
    display_fill_rect(8, 8, DISPLAY_WIDTH - 16, DISPLAY_HEIGHT - 16, card);
    display_fill_rect(8, 8, DISPLAY_WIDTH - 16, 4, accent);

    font_draw_text(20, 24, "ChatGPT", accent, card, 2);

    font_draw_text(20, 64, "MODEL", label, card, 1);
    font_draw_text(20, 134, "THINKING", label, card, 1);

    const int bar_x = 20;
    const int bar_y = 158;
    const int bar_w = DISPLAY_WIDTH - 48;
    const int bar_h = 10;
    const uint16_t track = display_rgb(40, 46, 62);
    const uint16_t bar_fill = accent;

    if (!fields->has_model) {
        font_draw_text(20, 82, "Waiting for bridge...", muted, card, 2);
        draw_thinking_bar(bar_x, bar_y, bar_w, bar_h, 0, THINKING_LEVEL_COUNT, track, bar_fill);
        font_draw_text(20, bar_y + bar_h + 8, "-", muted, card, 1);
    } else {
        draw_wrapped(20, 82, DISPLAY_WIDTH - 48, fields->model, text, card, 2);
        const char *thinking = fields->has_thinking ? fields->thinking : "-";
        const int level = fields->has_thinking ? catalog_thinking_level(thinking) : 0;
        draw_thinking_bar(bar_x, bar_y, bar_w, bar_h, level, THINKING_LEVEL_COUNT, track, bar_fill);
        font_draw_text(20, bar_y + bar_h + 8, thinking, text, card, 1);
    }

    /* Version bottom-right. */
    {
        const char *build = FIRMWARE_BUILD_STRING;
        const int scale = 1;
        const int pad = 12;
        const int bw = font_text_width(build, scale);
        const int bh = 7 * scale;
        const int bx = DISPLAY_WIDTH - pad - bw;
        const int by = DISPLAY_HEIGHT - pad - bh;
        font_draw_text(bx, by, build, muted, card, scale);
    }

    return display_flush();
}
