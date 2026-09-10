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

#if defined(AI_MODEL_PROFILE_SUPERMINI)
static esp_err_t ui_render_round(const model_fields_t *fields)
{
    const uint16_t bg = display_rgb(8, 10, 17);
    const uint16_t card = display_rgb(24, 28, 42);
    const uint16_t label = display_rgb(140, 150, 170);
    const uint16_t text = display_rgb(240, 244, 250);
    const uint16_t muted = display_rgb(110, 118, 135);
    const uint16_t clock = display_rgb(160, 168, 182);
    const uint16_t track = display_rgb(40, 46, 62);

    display_fill(bg);
    /* Leave a circular-safe margin; the glass masks the corners. */
    display_fill_rect(12, 12, 216, 216, card);

    const char *app = front_title_app() == DESK_OPENCODE ? "OPENCODE"
        : front_title_is_cursor() ? "CURSOR" : "CHATGPT";
    const int app_w = font_text_width(app, 1);
    font_draw_text((DISPLAY_WIDTH - app_w) / 2, 20, app, clock, card, 1);

    font_draw_text(28, 50, "MODEL", label, card, 1);
    if (!fields->has_model) {
        font_draw_text(28, 66, "WAITING", muted, card, 2);
    } else {
        draw_wrapped(28, 66, 184, fields->model, text, card, 2);
    }

    font_draw_text(28, 112, "THINKING", label, card, 1);
    const char *thinking = !fields->has_model ? "-"
        : catalog_thinking_count(fields->model) == 0 ? "UNSUPPORTED"
        : fields->has_thinking ? fields->thinking : "-";
    draw_wrapped(28, 128, 184, thinking, fields->has_model ? text : muted, card, 2);

    const int levels = fields->has_model ? catalog_thinking_count(fields->model) : 0;
    const int level = levels > 0 && fields->has_thinking
        ? catalog_thinking_level(fields->model, thinking) : 0;
    draw_thinking_bar(28, 166, 184, 8, level, levels > 0 ? levels : 1, track, text);

    const char *build = FIRMWARE_BUILD_STRING;
    const int bw = font_text_width(build, 1);
    font_draw_text((DISPLAY_WIDTH - bw) / 2, 202, build, muted, card, 1);

    esp_err_t err = display_flush();
    if (err == ESP_OK) {
        front_title_mark_drawn();
    }
    return err;
}
#endif

esp_err_t ui_render(const model_fields_t *fields)
{
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    return ui_render_round(fields);
#else
    const uint16_t bg = display_rgb(12, 14, 22);
    const uint16_t card = display_rgb(24, 28, 42);
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
    const logo_t *logo = front_title_app() == DESK_OPENCODE ? &logo_opencode
        : front_title_is_cursor() ? &logo_cursor : &logo_openai;
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
        draw_thinking_bar(20, bar_y, value_w, bar_h, 0, 1, track, text);
    } else {
        draw_wrapped(20, model_value_y, value_w, fields->model, text, card, 2);
        const char *thinking = catalog_thinking_count(fields->model) == 0
            ? "Unsupported" : (fields->has_thinking ? fields->thinking : "-");
        const int levels = catalog_thinking_count(fields->model);
        const int level = (levels > 0 && fields->has_thinking)
            ? catalog_thinking_level(fields->model, thinking) : 0;
        draw_wrapped(20, thinking_value_y, value_w, thinking, text, card, 2);
        draw_thinking_bar(20, bar_y, value_w, bar_h, level, levels > 0 ? levels : 1, track, text);
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
#endif
}

esp_err_t ui_render_screensaver(void)
{
    const uint16_t bg = display_rgb(12, 14, 22);
    const uint16_t clock = display_rgb(160, 168, 182);

    display_fill(bg);

    char time_text[16];
    char date_text[16];
    if (clock_format(time_text, sizeof(time_text))
        && clock_format_date(date_text, sizeof(date_text))) {
#if defined(AI_MODEL_PROFILE_SUPERMINI)
        const int time_scale = 3;
        const int date_scale = 1;
        const int time_h = 7 * time_scale;
        const int date_h = 7 * date_scale;
        const int gap = 10;
#else
        const int time_scale = 4;
        const int date_scale = 2;
        const int time_h = 7 * time_scale;
        const int date_h = 7 * date_scale;
        const int gap = 14;
#endif
        const int block_h = time_h + gap + date_h;
        const int time_w = font_text_width(time_text, time_scale);
        const int date_w = font_text_width(date_text, date_scale);
        const int time_y = (DISPLAY_HEIGHT - block_h) / 2;
        const int date_y = time_y + time_h + gap;
        font_draw_text((DISPLAY_WIDTH - time_w) / 2, time_y, time_text, clock, bg, time_scale);
        font_draw_text((DISPLAY_WIDTH - date_w) / 2, date_y, date_text, clock, bg, date_scale);
    }

    return display_flush();
}
