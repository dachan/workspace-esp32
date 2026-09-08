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

    const int pad = 20;
    const int title_scale = 2;
    const int date_scale = 1;
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
        const int tw = font_text_width(time_text, title_scale);
        const int dw = font_text_width(date_text, date_scale);
        font_draw_text(right - tw, header_y, time_text, accent, card, title_scale);
        font_draw_text(right - dw, header_y + title_h + 4, date_text, muted, card, date_scale);
    }

    const int model_label_y = 64;
    const int model_value_y = 82;
    /* Thinking reuses the MODEL label-to-value gap so both rows read alike. */
    const int value_gap = model_value_y - (model_label_y + 7);

    font_draw_text(20, model_label_y, "MODEL", label, card, 1);
    font_draw_text(20, 134, "THINKING", label, card, 1);

    const int bar_x = 20;
    const int bar_y = 158;
    const int bar_w = DISPLAY_WIDTH - 48;
    const int bar_h = 10;
    const uint16_t track = display_rgb(40, 46, 62);
    const uint16_t bar_fill = accent;

    if (!fields->has_model) {
        font_draw_text(20, model_value_y, "Waiting for bridge...", muted, card, 2);
        draw_thinking_bar(bar_x, bar_y, bar_w, bar_h, 0, catalog_thinking_count(fields->model), track, bar_fill);
        font_draw_text(20, bar_y + bar_h + value_gap, "-", muted, card, 1);
    } else {
        draw_wrapped(20, model_value_y, DISPLAY_WIDTH - 48, fields->model, text, card, 2);
        const char *thinking = catalog_thinking_count(fields->model) == 0
            ? "Unsupported" : (fields->has_thinking ? fields->thinking : "-");
        const int level = fields->has_thinking ? catalog_thinking_level(fields->model, thinking) : 0;
        draw_thinking_bar(bar_x, bar_y, bar_w, bar_h, level, catalog_thinking_count(fields->model), track, bar_fill);
        font_draw_text(20, bar_y + bar_h + value_gap, thinking, text, card, 1);
    }

    /* SYNC sits left of the version string on the same baseline row. */
    {
        const char *label_sync = "SYNC";
        const int scale = 1;
        const int pad_x = 14;
        const int pad_y = 10;
        const int tw = font_text_width(label_sync, scale);
        const int th = 7 * scale;
        s_sync_w = tw + pad_x * 2;
        s_sync_h = th + pad_y * 2;
        s_sync_x = 20;
        s_sync_y = DISPLAY_HEIGHT - 12 - s_sync_h;
        display_fill_rect(s_sync_x, s_sync_y, s_sync_w, s_sync_h, track);
        font_draw_text(s_sync_x + pad_x, s_sync_y + pad_y, label_sync, text, track, scale);
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
