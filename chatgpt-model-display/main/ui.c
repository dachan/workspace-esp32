#include "ui.h"

#include <math.h>
#include <string.h>

#include "build_number.h"
#include "catalog.h"
#include "clock.h"
#include "display.h"
#include "font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "front_title.h"
#include "logo.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
static int s_models_x;
static int s_models_y;
static int s_models_w;
static int s_models_h;
static int s_sync_angle;
static TickType_t s_sync_spin_start;
static TickType_t s_sync_spin_last;
static bool s_sync_spinning;

bool ui_hit_sync(int x, int y)
{
    const int pad = 6;
    return s_sync_w > 0 && x >= s_sync_x - pad && x < s_sync_x + s_sync_w + pad
        && y >= s_sync_y - pad && y < s_sync_y + s_sync_h + pad;
}

bool ui_hit_models(int x, int y)
{
    const int pad = 6;
    return s_models_w > 0 && x >= s_models_x - pad && x < s_models_x + s_models_w + pad
        && y >= s_models_y - pad && y < s_models_y + s_models_h + pad;
}

void ui_sync_pulse(void)
{
    TickType_t now = xTaskGetTickCount();
    s_sync_spin_start = now;
    s_sync_spin_last = now;
    s_sync_angle = 0;
    s_sync_spinning = true;
}

bool ui_sync_tick(void)
{
    if (!s_sync_spinning) {
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - s_sync_spin_start) >= pdMS_TO_TICKS(3000)) {
        s_sync_spinning = false;
        s_sync_angle = 0;
        return true;
    }
    if ((TickType_t)(now - s_sync_spin_last) < pdMS_TO_TICKS(40)) {
        return false;
    }
    s_sync_spin_last = now;
    s_sync_angle = (s_sync_angle + 30) % 360;
    return true;
}

static void draw_sync_dot(int x, int y, uint16_t fg)
{
    display_fill_rect(x, y, 2, 2, fg);
}

/* Classic circular-arrows refresh mark; `angle_deg` rotates the whole glyph. */
static void draw_sync_icon(int cx, int cy, int angle_deg, uint16_t fg)
{
    const float base = (float)angle_deg * (float)M_PI / 180.0f;
    const float r = 6.5f;

    for (int pass = 0; pass < 2; pass++) {
        const int start = pass == 0 ? 25 : 205;
        const int end = pass == 0 ? 155 : 335;
        float tip_x = 0, tip_y = 0, tip_tx = 0, tip_ty = 0;
        for (int a = start; a <= end; a += 5) {
            const float rad = base + (float)a * (float)M_PI / 180.0f;
            const float c = cosf(rad);
            const float s = sinf(rad);
            const int x = cx + (int)lroundf(r * c);
            const int y = cy + (int)lroundf(r * s);
            draw_sync_dot(x, y, fg);
            tip_x = (float)x;
            tip_y = (float)y;
            tip_tx = -s;
            tip_ty = c;
        }
        /* Arrowhead pointing along the arc tangent. */
        const int hx = (int)lroundf(tip_x + tip_tx * 4.0f);
        const int hy = (int)lroundf(tip_y + tip_ty * 4.0f);
        const int lx = (int)lroundf(tip_x - tip_ty * 3.0f - tip_tx * 1.5f);
        const int ly = (int)lroundf(tip_y + tip_tx * 3.0f - tip_ty * 1.5f);
        const int rx = (int)lroundf(tip_x + tip_ty * 3.0f - tip_tx * 1.5f);
        const int ry = (int)lroundf(tip_y - tip_tx * 3.0f - tip_ty * 1.5f);
        draw_sync_dot(hx, hy, fg);
        draw_sync_dot(lx, ly, fg);
        draw_sync_dot(rx, ry, fg);
        draw_sync_dot((hx + lx) / 2, (hy + ly) / 2, fg);
        draw_sync_dot((hx + rx) / 2, (hy + ry) / 2, fg);
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

esp_err_t ui_render(const model_fields_t *fields)
{
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
    const logo_t *logo = front_title_is_cursor() ? &logo_cursor : &logo_openai;
    if (front_title_app() == DESK_OPENCODE) {
        font_draw_text(pad, header_y, "OpenCode", text, card, title_scale);
    } else {
        display_blit_alpha(pad, header_y + title_h - logo->baseline, logo->width, logo->height,
                           logo->alpha, text, card);
    }
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

    /* SYNC: circular-arrows icon + label; icon spins after a tap. */
    {
        const char *label_sync = "SYNC";
        const int scale = 2;
        const int icon = 16;
        const int gap = 8;
        const int pad_x = 6;
        const int pad_y = 8;
        const int tw = font_text_width(label_sync, scale);
        const int th = 7 * scale;
        const int content_h = th > icon ? th : icon;
        s_sync_w = pad_x + icon + gap + tw + pad_x;
        s_sync_h = content_h + pad_y * 2;
        s_sync_x = 20;
        s_sync_y = DISPLAY_HEIGHT - 12 - s_sync_h;
        const int icon_cx = s_sync_x + pad_x + icon / 2;
        const int icon_cy = s_sync_y + s_sync_h / 2;
        const int text_x = s_sync_x + pad_x + icon + gap;
        const int text_y = s_sync_y + (s_sync_h - th) / 2;
        draw_sync_icon(icon_cx, icon_cy, s_sync_angle, text);
        font_draw_text(text_x, text_y, label_sync, text, card, scale);
    }

    s_models_w = 0;
    if (front_title_is_cursor()) {
        const char *label_models = "MODELS";
        const int scale = 2;
        const int pad_x = 6;
        const int pad_y = 8;
        const int tw = font_text_width(label_models, scale);
        const int th = 7 * scale;
        s_models_w = pad_x + tw + pad_x;
        s_models_h = th + pad_y * 2;
        s_models_x = s_sync_x + s_sync_w + 16;
        s_models_y = s_sync_y;
        const int text_x = s_models_x + pad_x;
        const int text_y = s_models_y + (s_models_h - th) / 2;
        font_draw_text(text_x, text_y, label_models, text, card, scale);
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

esp_err_t ui_render_screensaver(void)
{
    const uint16_t bg = display_rgb(12, 14, 22);
    const uint16_t clock = display_rgb(160, 168, 182);

    display_fill(bg);

    char time_text[16];
    char date_text[16];
    if (clock_format(time_text, sizeof(time_text))
        && clock_format_date(date_text, sizeof(date_text))) {
        const int time_scale = 4;
        const int date_scale = 2;
        const int time_h = 7 * time_scale;
        const int date_h = 7 * date_scale;
        const int gap = 14;
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
