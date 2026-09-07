#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "canvas.h"
#include "display.h"
#include "esp_log.h"
#include "font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_nvs.h"
#include "encoder.h"
#include "model_parse.h"
#include "serial_model.h"
#include "build_number.h"

static const char *TAG = "chatgpt_model";

typedef struct {
    model_fields_t fields;
    int waiting;
} ui_state_t;

static void draw_wrapped(int x, int y, int max_w, const char *text, uint16_t fg, uint16_t bg, int scale, int *out_y)
{
    // Word-wrap on spaces; hard-break overlong tokens.
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
    if (out_y) {
        *out_y = cy + line_h;
    }
}


static int thinking_level(const char *thinking)
{
    /* Map current and legacy ChatGPT thinking labels to 0..4 fill steps. */
    if (thinking == NULL || thinking[0] == '\0') {
        return 0;
    }
    char buf[MODEL_PARSE_MAX];
    size_t n = 0;
    for (const char *p = thinking; *p && n + 1 < sizeof(buf); p++) {
        buf[n++] = (char)tolower((unsigned char)*p);
    }
    buf[n] = '\0';

    if (strstr(buf, "heavy") || strstr(buf, "extra high") || strstr(buf, "max")) {
        return 4;
    }
    if (strstr(buf, "high") || strcmp(buf, "thinking") == 0 || strstr(buf, "advanced")) {
        return 3;
    }
    if (strstr(buf, "medium") || strstr(buf, "standard") || strstr(buf, "auto")) {
        return 2;
    }
    if (strstr(buf, "light") || strstr(buf, "instant") || strstr(buf, "fast") || strstr(buf, "low")) {
        return 1;
    }
    return 2; /* unknown but present */
}

#define THINKING_LEVEL_COUNT 4

static const char *thinking_name_for_level(int level)
{
    switch (level) {
    case 1: return "Light";
    case 2: return "Medium";
    case 3: return "High";
    case 4: return "Extra High";
    default: return "Medium";
    }
}

static int apply_thinking_level(ui_state_t *ui, int level)
{
    if (level < 1) {
        level = 1;
    }
    if (level > THINKING_LEVEL_COUNT) {
        level = THINKING_LEVEL_COUNT;
    }
    const char *name = thinking_name_for_level(level);
    int same = ui->fields.has_thinking && strcmp(ui->fields.thinking, name) == 0;
    snprintf(ui->fields.thinking, sizeof(ui->fields.thinking), "%s", name);
    ui->fields.has_thinking = 1;
    if (!ui->fields.has_model) {
        snprintf(ui->fields.model, sizeof(ui->fields.model), "%s", "GPT-5.6 Luna");
        ui->fields.has_model = 1;
        ui->waiting = 0;
    }
    (void)model_nvs_save(&ui->fields);
    return !same;
}

/* Preset model names for the model encoder (desk UI). */
static const char *s_models[] = {
    "GPT-6 Astra",
    "GPT-5.6 Sol",
    "GPT-5.6 Terra",
    "GPT-5.6 Luna",
    "GPT-5.5",
    "GPT-5.4 Mini",
};
static const int s_models_n = (int)(sizeof(s_models) / sizeof(s_models[0]));

static int model_index(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (int i = 0; i < s_models_n; i++) {
        if (strcasecmp(name, s_models[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int apply_model_delta(ui_state_t *ui, int delta)
{
    int idx = model_index(ui->fields.has_model ? ui->fields.model : NULL);
    int next = idx < 0 ? 0 : idx + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= s_models_n) {
        next = s_models_n - 1;
    }
    if (idx >= 0 && next == idx && ui->fields.has_model) {
        return 0;
    }
    snprintf(ui->fields.model, sizeof(ui->fields.model), "%s", s_models[next]);
    ui->fields.has_model = 1;
    ui->waiting = 0;
    if (!ui->fields.has_thinking) {
        snprintf(ui->fields.thinking, sizeof(ui->fields.thinking), "%s", "Medium");
        ui->fields.has_thinking = 1;
    }
    (void)model_nvs_save(&ui->fields);
    return 1;
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

static void ui_render(const ui_state_t *ui)
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

    if (ui->waiting || !ui->fields.has_model) {
        font_draw_text(20, 82, "Waiting for bridge...", muted, card, 2);
        draw_thinking_bar(bar_x, bar_y, bar_w, bar_h, 0, THINKING_LEVEL_COUNT, track, bar_fill);
        font_draw_text(20, bar_y + bar_h + 8, "—", muted, card, 1);
    } else {
        int y_after = 82;
        draw_wrapped(20, 82, DISPLAY_WIDTH - 48, ui->fields.model, text, card, 2, &y_after);
        const char *thinking = ui->fields.has_thinking ? ui->fields.thinking : "—";
        const int level = ui->fields.has_thinking ? thinking_level(thinking) : 0;
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

    display_flush();
}

void app_main(void)
{
    ESP_LOGI(TAG, "chatgpt-model-display starting");
    ESP_ERROR_CHECK(model_nvs_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_set_backlight(80));
    ESP_ERROR_CHECK(serial_model_init());
    ESP_ERROR_CHECK(encoder_init());

    ui_state_t ui = {
        .waiting = 1,
    };
    if (model_nvs_load(&ui.fields)) {
        ui.waiting = 0;
        ESP_LOGI(TAG, "boot from NVS cache");
    }
    ui_render(&ui);

    TickType_t last_paint = xTaskGetTickCount();
    /* Ignore stale Mac MODEL/THINKING for a short window after a local encoder SET. */
    TickType_t hold_rx_until = 0;
    const TickType_t hold_rx_ticks = pdMS_TO_TICKS(8000);

    while (1) {
        int think_d = encoder_delta(ENCODER_THINKING);
        if (think_d != 0) {
            int level = ui.fields.has_thinking ? thinking_level(ui.fields.thinking) : 2;
            if (level < 1) {
                level = 2;
            }
            if (apply_thinking_level(&ui, level + think_d)) {
                serial_model_send_set_thinking(ui.fields.thinking);
                hold_rx_until = xTaskGetTickCount() + hold_rx_ticks;
                ui_render(&ui);
                last_paint = xTaskGetTickCount();
            }
        }
        if (encoder_button_pressed(ENCODER_THINKING)) {
            int level = ui.fields.has_thinking ? thinking_level(ui.fields.thinking) : 0;
            int next = level < 1 ? 1
                                 : (level >= THINKING_LEVEL_COUNT ? THINKING_LEVEL_COUNT : level + 1);
            if (apply_thinking_level(&ui, next)) {
                serial_model_send_set_thinking(ui.fields.thinking);
                hold_rx_until = xTaskGetTickCount() + hold_rx_ticks;
                ui_render(&ui);
                last_paint = xTaskGetTickCount();
            }
        }

        int model_d = encoder_delta(ENCODER_MODEL);
        if (model_d != 0) {
            if (apply_model_delta(&ui, model_d)) {
                serial_model_send_set_model(ui.fields.model);
                hold_rx_until = xTaskGetTickCount() + hold_rx_ticks;
                ui_render(&ui);
                last_paint = xTaskGetTickCount();
            } else {
                encoder_clear_partial(ENCODER_MODEL);
            }
        }
        if (encoder_button_pressed(ENCODER_MODEL)) {
            if (apply_model_delta(&ui, 1)) {
                serial_model_send_set_model(ui.fields.model);
                hold_rx_until = xTaskGetTickCount() + hold_rx_ticks;
                ui_render(&ui);
                last_paint = xTaskGetTickCount();
            }
        }

        model_fields_t next = ui.fields;
        if (serial_model_poll(&next)) {
            TickType_t now = xTaskGetTickCount();
            int held = hold_rx_until != 0 && now < hold_rx_until;
            if (held) {
                ESP_LOGD(TAG, "hold Mac poll after local encoder SET");
            } else {
                ui.fields = next;
                ui.waiting = !ui.fields.has_model;
                if (ui.fields.has_model) {
                    (void)model_nvs_save(&ui.fields);
                }
                ui_render(&ui);
                last_paint = xTaskGetTickCount();
            }
        }

        // Soft blink of waiting hint every ~1s without clobbering a live model.
        if (ui.waiting && (xTaskGetTickCount() - last_paint) > pdMS_TO_TICKS(1000)) {
            ui_render(&ui);
            last_paint = xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
