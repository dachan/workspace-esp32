#include "cursor_settings.h"

#include "catalog.h"
#include "display.h"
#include "font.h"
#include "front_title.h"

#define ROW_H 32
#define LIST_Y 50
#define LIST_BOTTOM 8
#define HEADER_PAD 20

static bool s_open;
static bool s_mask_changed;
static int s_highlight;
static int s_scroll;
static int s_done_x, s_done_y, s_done_w, s_done_h;

static int visible_rows(void)
{
    int h = DISPLAY_HEIGHT - LIST_Y - LIST_BOTTOM;
    int n = h / ROW_H;
    return n < 1 ? 1 : n;
}

static void reveal(void)
{
    int vis = visible_rows();
    int count = catalog_cursor_slot_count();
    if (s_highlight < 0) {
        s_highlight = 0;
    }
    if (count > 0 && s_highlight >= count) {
        s_highlight = count - 1;
    }
    if (s_highlight < s_scroll) {
        s_scroll = s_highlight;
    }
    if (s_highlight >= s_scroll + vis) {
        s_scroll = s_highlight - vis + 1;
    }
    int max_scroll = count > vis ? count - vis : 0;
    if (s_scroll < 0) {
        s_scroll = 0;
    }
    if (s_scroll > max_scroll) {
        s_scroll = max_scroll;
    }
}

static bool hit_done(int x, int y)
{
    const int pad = 6;
    return s_done_w > 0 && x >= s_done_x - pad && x < s_done_x + s_done_w + pad
        && y >= s_done_y - pad && y < s_done_y + s_done_h + pad;
}

static int row_at(int x, int y)
{
    (void)x;
    if (y < LIST_Y || y >= DISPLAY_HEIGHT - LIST_BOTTOM) {
        return -1;
    }
    int row = s_scroll + (y - LIST_Y) / ROW_H;
    if (row < 0 || row >= catalog_cursor_slot_count()) {
        return -1;
    }
    if (row >= s_scroll + visible_rows()) {
        return -1;
    }
    return row;
}

static void move_highlight(int delta)
{
    if (!delta) {
        return;
    }
    int count = catalog_cursor_slot_count();
    if (count <= 0) {
        return;
    }
    s_highlight += delta;
    if (s_highlight < 0) {
        s_highlight = 0;
    }
    if (s_highlight >= count) {
        s_highlight = count - 1;
    }
    reveal();
}

static bool toggle_slot(int slot)
{
    if (slot < 0 || slot >= catalog_cursor_slot_count()) {
        return false;
    }
    bool on = !catalog_cursor_slot_on(slot);
    if (!catalog_cursor_set_slot(slot, on)) {
        return false;
    }
    s_mask_changed = true;
    return true;
}

void cursor_settings_open(void)
{
    s_open = true;
    s_highlight = 0;
    s_scroll = 0;
    reveal();
}

void cursor_settings_close(void)
{
    s_open = false;
}

bool cursor_settings_is_open(void)
{
    return s_open;
}

bool cursor_settings_handle(const touch_sample_t *touch, int model_delta, int think_delta,
                            bool model_pressed, bool think_pressed)
{
    if (!s_open) {
        return false;
    }
    if (!front_title_is_cursor()) {
        s_open = false;
        return true;
    }
    bool paint = false;
    int before = s_highlight;
    int scroll_before = s_scroll;
    move_highlight(model_delta + think_delta);
    if (s_highlight != before || s_scroll != scroll_before) {
        paint = true;
    }
    if (model_pressed || think_pressed) {
        paint |= toggle_slot(s_highlight);
    }
    if (touch && touch->released && touch->held_ms < 800) {
        if (hit_done(touch->x, touch->y)) {
            s_open = false;
            return true;
        }
        int row = row_at(touch->x, touch->y);
        if (row >= 0) {
            s_highlight = row;
            reveal();
            paint |= toggle_slot(row);
            paint = true;
        }
    }
    return paint;
}

bool cursor_settings_take_mask_changed(void)
{
    bool changed = s_mask_changed;
    s_mask_changed = false;
    return changed;
}

esp_err_t cursor_settings_render(void)
{
    const uint16_t bg = display_rgb(12, 14, 22);
    const uint16_t card = display_rgb(24, 28, 42);
    const uint16_t sel = display_rgb(36, 42, 62);
    const uint16_t text = display_rgb(240, 244, 250);
    const uint16_t muted = display_rgb(110, 118, 135);
    const uint16_t track = display_rgb(40, 46, 62);
    const uint16_t on = display_rgb(52, 168, 96);

    display_fill(bg);
    display_fill_rect(8, 8, DISPLAY_WIDTH - 16, DISPLAY_HEIGHT - 16, card);

    font_draw_text(HEADER_PAD, 18, "MODELS", text, card, 2);

    const char *done = "DONE";
    const int scale = 2;
    const int tw = font_text_width(done, scale);
    const int th = 7 * scale;
    s_done_w = tw + 12;
    s_done_h = th + 10;
    s_done_x = DISPLAY_WIDTH - HEADER_PAD - s_done_w;
    s_done_y = 12;
    font_draw_text(s_done_x + 6, s_done_y + (s_done_h - th) / 2, done, text, card, scale);

    const int vis = visible_rows();
    const int count = catalog_cursor_slot_count();
    const int value_right = DISPLAY_WIDTH - HEADER_PAD - 52;
    for (int i = 0; i < vis; i++) {
        int slot = s_scroll + i;
        if (slot >= count) {
            break;
        }
        int y = LIST_Y + i * ROW_H;
        uint16_t row_bg = slot == s_highlight ? sel : card;
        if (slot == s_highlight) {
            display_fill_rect(12, y, DISPLAY_WIDTH - 24, ROW_H, row_bg);
        }
        const char *name = catalog_cursor_slot_name(slot);
        font_draw_text(HEADER_PAD, y + (ROW_H - 14) / 2, name, text, row_bg, 2);
        bool enabled = catalog_cursor_slot_on(slot);
        int tx = value_right;
        int ty = y + (ROW_H - 18) / 2;
        display_fill_rect(tx, ty, 44, 18, track);
        if (enabled) {
            display_fill_rect(tx + 22, ty + 2, 20, 14, on);
        } else {
            display_fill_rect(tx + 2, ty + 2, 20, 14, muted);
        }
    }

    return display_flush();
}
