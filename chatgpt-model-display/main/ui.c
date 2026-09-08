#include "ui.h"

#include <string.h>

#include "build_number.h"
#include "catalog.h"
#include "clock.h"
#include "display.h"
#include "font.h"
#include "front_title.h"
#include "logo.h"

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


static int s_sync_x;
static int s_sync_y;
static int s_sync_w;
static int s_sync_h;

bool ui_hit_sync(int x, int y)
{
    const int pad = 6;
    return s_sync_w > 0 && x >= s_sync_x - pad && x < s_sync_x + s_sync_w + pad
        && y >= s_sync_y - pad && y < s_sync_y + s_sync_h + pad;
}

static void draw_thinking_bar(int x, int y, int w, int h, int level, int max_level,
                              uint16_t track, uint16_t fill)
{
    /* Segmented meter: discrete pills with gaps. */
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
    const uint16_t clock = display_rgb(160, 168, 182);
    const uint16_t track = display_rgb(40, 46, 62);

    display_fill(bg);
    display_fill_rect(8, 8, DISPLAY_WIDTH - 16, DISPLAY_HEIGHT - 16, card);

    const int pad = 20;
    const int title_scale = 2;
    const int title_h = 7 * title_scale;
    const int header_y = 20;
    char time_text[16];
    char date_text[16];
    const int have_clock = clock_format(time_text, sizeof(time_text))
        && clock_format_date(date_text, sizeof(date_text));
    /* Brand lockup sits on the same baseline the title text used. */
    const logo_t *logo = front_title_is_cursor() ? &logo_cursor : &logo_openai;
    display_blit_alpha(pad, header_y + title_h - logo->baseline, logo->width, logo->height,
                       logo->alpha, text, card);
    if (have_clock) {
        const int right = DISPLAY_WIDTH - pad;
        const int gap = 6 * title_scale;
        const int tw = font_text_width(time_text, title_scale);
        const int dw = font_text_width(date_text, title_scale);
        const int time_x = right - tw;
        const int date_x = time_x - gap - dw;
        font_draw_text(date_x, header_y, date_text, clock, card, title_scale);
        font_draw_text(time_x, header_y, time_text, clock, card, title_scale);
    }

    const int model_label_y = 64;
    const int model_value_y = 82;
    /* Match MODEL: label scale 1, value scale 2 with the same label-to-value gap. */
    const int row_gap = model_value_y - model_label_y;
    const int model_value_h = 7 * 2;
    const int thinking_label_y = model_value_y + model_value_h + 32;
    const int thinking_value_y = thinking_label_y + row_gap;
    const int thinking_value_h = 7 * 2;
    const int bar_y = thinking_value_y + thinking_value_h + 10;
    const int bar_h = 10;
    const int value_w = DISPLAY_WIDTH - 48;

    font_draw_text(20, model_label_y, "MODEL", label, card, 1);
    font_draw_text(20, thinking_label_y, "THINKING", label, card, 1);

    if (!fields->has_model) {
        font_draw_text(20, model_value_y, "Waiting for bridge...", muted, card, 2);
        font_draw_text(20, thinking_value_y, "-", muted, card, 2);
        draw_thinking_bar(20, bar_y, value_w, bar_h, 0, 1, track, accent);
    } else {
        draw_wrapped(20, model_value_y, value_w, fields->model, text, card, 2);
        const char *thinking = catalog_thinking_count(fields->model) == 0
            ? "Unsupported" : (fields->has_thinking ? fields->thinking : "-");
        const int levels = catalog_thinking_count(fields->model);
        const int level = (levels > 0 && fields->has_thinking)
            ? catalog_thinking_level(fields->model, thinking) : 0;
        draw_wrapped(20, thinking_value_y, value_w, thinking, text, card, 2);
        draw_thinking_bar(20, bar_y, value_w, bar_h, level, levels > 0 ? levels : 1, track, accent);
    }

    /* SYNC sits left of the version string on the same baseline row. */
    {
        const char *label_sync = "SYNC";
        const uint16_t orange = display_rgb(255, 122, 47);
        const int scale = 2;
        const int pad_x = 22;
        const int pad_y = 14;
        const int tw = font_text_width(label_sync, scale);
        const int th = 7 * scale;
        s_sync_w = tw + pad_x * 2;
        s_sync_h = th + pad_y * 2;
        s_sync_x = 20;
        s_sync_y = DISPLAY_HEIGHT - 12 - s_sync_h;
        display_fill_rect(s_sync_x, s_sync_y, s_sync_w, s_sync_h, orange);
        font_draw_text(s_sync_x + pad_x, s_sync_y + pad_y, label_sync, text, orange, scale);
    }

    /* Version bottom-right. */
    {
        const char *build = FIRMWARE_BUILD_STRING;
        const int bw = font_text_width(build, 1);
        font_draw_text(DISPLAY_WIDTH - 12 - bw, DISPLAY_HEIGHT - 12 - 7, build, muted, card, 1);
    }

    esp_err_t err = display_flush();
    if (err == ESP_OK) {
        front_title_mark_drawn();
    }
    return err;
}
