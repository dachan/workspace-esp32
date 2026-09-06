#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "display.h"
#include "esp_log.h"
#include "font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_nvs.h"
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

    if (ui->waiting || !ui->fields.has_model) {
        font_draw_text(20, 82, "Waiting for bridge...", muted, card, 2);
        font_draw_text(20, 152, "—", muted, card, 2);
    } else {
        int y_after = 82;
        draw_wrapped(20, 82, DISPLAY_WIDTH - 48, ui->fields.model, text, card, 2, &y_after);
        const char *thinking = ui->fields.has_thinking ? ui->fields.thinking : "—";
        font_draw_text(20, 152, thinking, text, card, 2);
    }

    /* Build number, muted, bottom-right inside the card. */
    {
        const char *build = FIRMWARE_BUILD_STRING;
        const int scale = 1;
        const int pad = 12;
        const int bw = font_text_width(build, scale);
        const int bx = DISPLAY_WIDTH - pad - bw;
        const int by = DISPLAY_HEIGHT - pad - 8 * scale;
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

    ui_state_t ui = {
        .waiting = 1,
    };
    if (model_nvs_load(&ui.fields)) {
        ui.waiting = 0;
        ESP_LOGI(TAG, "boot from NVS cache");
    }
    ui_render(&ui);

    TickType_t last_paint = xTaskGetTickCount();
    while (1) {
        model_fields_t next = ui.fields;
        if (serial_model_poll(&next)) {
            ui.fields = next;
            ui.waiting = !ui.fields.has_model;
            if (ui.fields.has_model) {
                (void)model_nvs_save(&ui.fields);
            }
            ui_render(&ui);
            last_paint = xTaskGetTickCount();
        }

        // Soft blink of waiting hint every ~1s without clobbering a live model.
        if (ui.waiting && (xTaskGetTickCount() - last_paint) > pdMS_TO_TICKS(1000)) {
            ui_render(&ui);
            last_paint = xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
