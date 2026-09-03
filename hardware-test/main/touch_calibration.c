#include "touch_calibration.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "display_sync.h"
#include "display_profile.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "touch_calibration";

enum {
    TARGET_COUNT = 5,
    SAMPLE_COUNT = 10,
    POWER_OFF_HOLD_MS = 3000,
    TOUCH_RELEASE_MS = 120,
    GLYPH_WIDTH = 5,
    GLYPH_HEIGHT = 7,
    GLYPH_ADVANCE = 6,
    LABEL_SCALE = 3,
};

typedef struct {
    uint16_t screen_x;
    uint16_t screen_y;
    uint16_t raw_x;
    uint16_t raw_y;
} calibration_point_t;

static uint16_t rgb565_wire(uint8_t red, uint8_t green, uint8_t blue)
{
    uint16_t value = ((uint16_t)(red & 0xf8) << 8)
                   | ((uint16_t)(green & 0xfc) << 3)
                   | ((uint16_t)blue >> 3);
    return __builtin_bswap16(value);
}

static void draw_rect(esp_lcd_panel_handle_t panel, int x, int y, int width, int height,
                      uint16_t color)
{
    const int rows_per_chunk = 16;
    uint16_t *pixels = heap_caps_malloc(width * rows_per_chunk * sizeof(*pixels), MALLOC_CAP_DMA);
    if (!pixels) {
        ESP_LOGE(TAG, "draw buffer allocation failed");
        return;
    }
    for (int i = 0; i < width * rows_per_chunk; ++i) {
        pixels[i] = color;
    }
    for (int row = 0; row < height; row += rows_per_chunk) {
        int rows = height - row < rows_per_chunk ? height - row : rows_per_chunk;
        ESP_ERROR_CHECK(display_sync_draw(panel, x, y + row, x + width, y + row + rows,
                                          pixels));
    }
    free(pixels);
}

static void draw_target(esp_lcd_panel_handle_t panel, uint16_t x, uint16_t y)
{
    draw_rect(panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, rgb565_wire(0, 0, 0));
    draw_rect(panel, x - 14, y - 2, 29, 5, rgb565_wire(255, 255, 255));
    draw_rect(panel, x - 2, y - 14, 5, 29, rgb565_wire(255, 255, 255));
    draw_rect(panel, x - 5, y - 5, 11, 11, rgb565_wire(255, 0, 0));
}

static uint8_t glyph_row(char character, int row)
{
    static const uint8_t e[GLYPH_HEIGHT] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
    static const uint8_t f[GLYPH_HEIGHT] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 };
    static const uint8_t l[GLYPH_HEIGHT] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f };
    static const uint8_t o[GLYPH_HEIGHT] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
    static const uint8_t p[GLYPH_HEIGHT] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 };
    static const uint8_t r[GLYPH_HEIGHT] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 };
    static const uint8_t s[GLYPH_HEIGHT] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e };
    static const uint8_t w[GLYPH_HEIGHT] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 };
    const uint8_t *glyph = NULL;
    switch (character) {
    case 'E': glyph = e; break;
    case 'F': glyph = f; break;
    case 'L': glyph = l; break;
    case 'O': glyph = o; break;
    case 'P': glyph = p; break;
    case 'R': glyph = r; break;
    case 'S': glyph = s; break;
    case 'W': glyph = w; break;
    default: return 0;
    }
    return glyph[row];
}

static void draw_label(esp_lcd_panel_handle_t panel, int origin_x, int origin_y,
                       const char *text, uint16_t color)
{
    // Characters and glyph bits are drawn mirrored in X. After MADCTL,
    // framebuffer +X is visual left; this makes labels read LTR. See AGENTS.md.
    int length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    int glyph_w = GLYPH_WIDTH * LABEL_SCALE;
    int glyph_h = GLYPH_HEIGHT * LABEL_SCALE;
    uint16_t *pixels = heap_caps_malloc(glyph_w * glyph_h * sizeof(*pixels), MALLOC_CAP_DMA);
    if (!pixels) {
        return;
    }
    for (int i = 0; i < length; ++i) {
        for (int row = 0; row < GLYPH_HEIGHT; ++row) {
            uint8_t bits = glyph_row(text[i], row);
            for (int sy = 0; sy < LABEL_SCALE; ++sy) {
                uint16_t *dest = pixels + (row * LABEL_SCALE + sy) * glyph_w;
                for (int col = 0; col < GLYPH_WIDTH; ++col) {
                    uint16_t pixel = (bits & (1U << col)) ? color : 0;
                    for (int sx = 0; sx < LABEL_SCALE; ++sx) {
                        dest[col * LABEL_SCALE + sx] = pixel;
                    }
                }
            }
        }
        int x = origin_x + (length - 1 - i) * GLYPH_ADVANCE * LABEL_SCALE;
        ESP_ERROR_CHECK(display_sync_draw(panel, x, origin_y, x + glyph_w,
                                          origin_y + glyph_h, pixels));
    }
    free(pixels);
}

static void choice_layout(int *x, int *width, int *height, int *sleep_y, int *power_y)
{
    int gap = LCD_HEIGHT / 16;
    int margin = LCD_WIDTH / 16;
    if (gap < 12) {
        gap = 12;
    }
    if (margin < 10) {
        margin = 10;
    }
    *x = margin;
    *width = LCD_WIDTH - 2 * margin;
    *height = (LCD_HEIGHT - 3 * gap) / 2;
    *sleep_y = gap;
    *power_y = gap * 2 + *height;
}

static void draw_choice_button(esp_lcd_panel_handle_t panel, int x, int y, int width,
                               int height, const char *label, uint16_t green)
{
    const int border = 4;
    draw_rect(panel, x, y, width, height, green);
    draw_rect(panel, x + border, y + border, width - 2 * border, height - 2 * border,
              rgb565_wire(0, 0, 0));
    int length = 0;
    while (label[length] != '\0') {
        ++length;
    }
    int text_w = length * GLYPH_ADVANCE * LABEL_SCALE - LABEL_SCALE;
    int text_h = GLYPH_HEIGHT * LABEL_SCALE;
    int text_x = x + (width - text_w) / 2;
    int text_y = y + (height - text_h) / 2;
    draw_label(panel, text_x, text_y, label, green);
}

static void map_choice_touch(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    touch_calibration_t mapping;
    if (touch_calibration_load(&mapping)) {
        touch_calibration_apply(&mapping, raw_x, raw_y, x, y);
        return;
    }
    display_profile_touch_to_view(raw_x, raw_y, x, y);
}

static void offer_hold_actions(esp_lcd_panel_handle_t panel, touch_read_fn_t touch_read,
                               touch_hold_fn_t touch_hold)
{
    uint16_t green = rgb565_wire(0, 255, 0);
    int x;
    int width;
    int height;
    int sleep_y;
    int power_y;
    choice_layout(&x, &width, &height, &sleep_y, &power_y);
    draw_rect(panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, rgb565_wire(0, 0, 0));
    draw_choice_button(panel, x, sleep_y, width, height, "SLEEP", green);
    draw_choice_button(panel, x, power_y, width, height, "POWER OFF", green);
    ESP_LOGI(TAG, "HOLD MENU: tap SLEEP or POWER OFF");

    uint16_t ignored_x;
    uint16_t ignored_y;
    while (touch_read(&ignored_x, &ignored_y)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(TOUCH_RELEASE_MS));

    while (true) {
        uint16_t raw_x;
        uint16_t raw_y;
        if (!touch_read(&raw_x, &raw_y)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        uint16_t screen_x;
        uint16_t screen_y;
        map_choice_touch(raw_x, raw_y, &screen_x, &screen_y);
        bool in_x = screen_x >= (uint16_t)x && screen_x < (uint16_t)(x + width);
        if (in_x && screen_y >= (uint16_t)sleep_y
            && screen_y < (uint16_t)(sleep_y + height)) {
            ESP_LOGI(TAG, "SLEEP requested");
            touch_hold(TOUCH_HOLD_SLEEP);
        } else if (in_x && screen_y >= (uint16_t)power_y
                   && screen_y < (uint16_t)(power_y + height)) {
            ESP_LOGI(TAG, "POWER OFF requested");
            touch_hold(TOUCH_HOLD_POWER_OFF);
        }
        while (touch_read(&ignored_x, &ignored_y)) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void offer_hold_if_held(esp_lcd_panel_handle_t panel, touch_read_fn_t touch_read,
                               touch_hold_fn_t touch_hold, TickType_t hold_started_at)
{
    if (!touch_hold || hold_started_at == 0) {
        return;
    }
    if (xTaskGetTickCount() - hold_started_at < pdMS_TO_TICKS(POWER_OFF_HOLD_MS)) {
        return;
    }
    ESP_LOGI(TAG, "CALIBRATION HOLD: three seconds");
    offer_hold_actions(panel, touch_read, touch_hold);
}

static bool wait_until_released(esp_lcd_panel_handle_t panel, touch_read_fn_t touch_read,
                                touch_hold_fn_t touch_hold, TickType_t deadline)
{
    TickType_t hold_started_at = 0;
    TickType_t last_contact_at = 0;
    bool in_contact = false;
    while (xTaskGetTickCount() < deadline) {
        uint16_t ignored_x;
        uint16_t ignored_y;
        TickType_t now = xTaskGetTickCount();
        if (touch_read(&ignored_x, &ignored_y)) {
            if (!in_contact) {
                hold_started_at = now;
                in_contact = true;
            }
            last_contact_at = now;
            offer_hold_if_held(panel, touch_read, touch_hold, hold_started_at);
        } else if (!in_contact
                   || now - last_contact_at >= pdMS_TO_TICKS(TOUCH_RELEASE_MS)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

static bool read_raw_point(esp_lcd_panel_handle_t panel, touch_read_fn_t touch_read,
                           touch_hold_fn_t touch_hold, uint16_t *raw_x, uint16_t *raw_y)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
    if (!wait_until_released(panel, touch_read, touch_hold, deadline)) {
        return false;
    }

    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    unsigned int samples = 0;
    TickType_t hold_started_at = 0;
    TickType_t last_contact_at = 0;
    bool in_contact = false;
    while (xTaskGetTickCount() < deadline) {
        uint16_t sample_x;
        uint16_t sample_y;
        TickType_t now = xTaskGetTickCount();
        if (touch_read(&sample_x, &sample_y)) {
            if (!in_contact) {
                hold_started_at = now;
                sum_x = 0;
                sum_y = 0;
                samples = 0;
                in_contact = true;
            }
            last_contact_at = now;
            sum_x += sample_x;
            sum_y += sample_y;
            ++samples;
            offer_hold_if_held(panel, touch_read, touch_hold, hold_started_at);
        } else if (in_contact
                   && now - last_contact_at >= pdMS_TO_TICKS(TOUCH_RELEASE_MS)) {
            in_contact = false;
            if (samples >= SAMPLE_COUNT) {
                *raw_x = sum_x / samples;
                *raw_y = sum_y / samples;
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

static bool solve_3x3(double matrix[3][4], float result[3])
{
    for (int pivot = 0; pivot < 3; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 3; ++row) {
            if (fabs(matrix[row][pivot]) > fabs(matrix[best][pivot])) {
                best = row;
            }
        }
        if (fabs(matrix[best][pivot]) < 0.0001) {
            return false;
        }
        if (best != pivot) {
            for (int column = pivot; column < 4; ++column) {
                double swap = matrix[pivot][column];
                matrix[pivot][column] = matrix[best][column];
                matrix[best][column] = swap;
            }
        }
        double scale = matrix[pivot][pivot];
        for (int column = pivot; column < 4; ++column) {
            matrix[pivot][column] /= scale;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == pivot) {
                continue;
            }
            double factor = matrix[row][pivot];
            for (int column = pivot; column < 4; ++column) {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
        }
    }
    for (int row = 0; row < 3; ++row) {
        result[row] = matrix[row][3];
    }
    return true;
}

static bool fit_calibration(const calibration_point_t points[TARGET_COUNT],
                            touch_calibration_t *calibration)
{
    double normal[3][3] = {{0}};
    double target_x[3] = {0};
    double target_y[3] = {0};

    for (int i = 0; i < TARGET_COUNT; ++i) {
        double raw[3] = {1.0, points[i].raw_x, points[i].raw_y};
        for (int row = 0; row < 3; ++row) {
            target_x[row] += raw[row] * points[i].screen_x;
            target_y[row] += raw[row] * points[i].screen_y;
            for (int column = 0; column < 3; ++column) {
                normal[row][column] += raw[row] * raw[column];
            }
        }
    }

    double x_matrix[3][4];
    double y_matrix[3][4];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            x_matrix[row][column] = normal[row][column];
            y_matrix[row][column] = normal[row][column];
        }
        x_matrix[row][3] = target_x[row];
        y_matrix[row][3] = target_y[row];
    }

    float x_coefficients[3];
    float y_coefficients[3];
    if (!solve_3x3(x_matrix, x_coefficients) || !solve_3x3(y_matrix, y_coefficients)) {
        return false;
    }
    calibration->x_offset = x_coefficients[0];
    calibration->x_from_raw_x = x_coefficients[1];
    calibration->x_from_raw_y = x_coefficients[2];
    calibration->y_offset = y_coefficients[0];
    calibration->y_from_raw_x = y_coefficients[1];
    calibration->y_from_raw_y = y_coefficients[2];
    return true;
}

void touch_calibration_apply(const touch_calibration_t *calibration, uint16_t raw_x,
                             uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y)
{
    float x = calibration->x_offset + calibration->x_from_raw_x * raw_x
            + calibration->x_from_raw_y * raw_y;
    float y = calibration->y_offset + calibration->y_from_raw_x * raw_x
            + calibration->y_from_raw_y * raw_y;
    *screen_x = (uint16_t)fmaxf(0, fminf(LCD_WIDTH - 1, roundf(x)));
    *screen_y = (uint16_t)fmaxf(0, fminf(LCD_HEIGHT - 1, roundf(y)));
}

static bool save_calibration(const touch_calibration_t *calibration)
{
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(error));
        return false;
    }
    nvs_handle_t handle;
    error = nvs_open("touch", NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_blob(handle, DISPLAY_CALIBRATION_KEY, calibration, sizeof(*calibration));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool touch_calibration_load(touch_calibration_t *result)
{
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return false;
    }
    nvs_handle_t handle;
    error = nvs_open("touch", NVS_READONLY, &handle);
    if (error != ESP_OK) {
        return false;
    }
    size_t length = sizeof(*result);
    error = nvs_get_blob(handle, DISPLAY_CALIBRATION_KEY, result, &length);
    nvs_close(handle);
    return error == ESP_OK && length == sizeof(*result)
        && isfinite(result->x_offset) && isfinite(result->x_from_raw_x)
        && isfinite(result->x_from_raw_y) && isfinite(result->y_offset)
        && isfinite(result->y_from_raw_x) && isfinite(result->y_from_raw_y);
}

bool touch_calibration_run(esp_lcd_panel_handle_t panel, touch_read_fn_t touch_read,
                           touch_hold_fn_t touch_hold, gpio_num_t backlight_pin,
                           touch_calibration_t *result)
{
    calibration_point_t points[TARGET_COUNT] = {0};

    gpio_set_level(backlight_pin, 1);
    ESP_LOGI(TAG, "CALIBRATION START: tap the centre of each target: top-left, top-right, bottom-right, bottom-left, centre");
    for (int i = 0; i < TARGET_COUNT; ++i) {
        points[i].screen_x = display_calibration_targets[i][0];
        points[i].screen_y = display_calibration_targets[i][1];
        draw_target(panel, points[i].screen_x, points[i].screen_y);
        if (!read_raw_point(panel, touch_read, touch_hold,
                            &points[i].raw_x, &points[i].raw_y)) {
            ESP_LOGE(TAG, "CALIBRATION TIMEOUT at target %d", i + 1);
            gpio_set_level(backlight_pin, 0);
            return false;
        }
        ESP_LOGI(TAG, "CALIBRATION TARGET %d/%d raw=(%u,%u) screen=(%u,%u)",
                 i + 1, TARGET_COUNT, points[i].raw_x, points[i].raw_y,
                 points[i].screen_x, points[i].screen_y);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    bool fitted = fit_calibration(points, result);
    bool saved = fitted && save_calibration(result);
    if (saved) {
        float max_error = 0;
        for (int i = 0; i < TARGET_COUNT; ++i) {
            uint16_t x;
            uint16_t y;
            touch_calibration_apply(result, points[i].raw_x, points[i].raw_y, &x, &y);
            float error = hypotf((float)x - points[i].screen_x, (float)y - points[i].screen_y);
            if (error > max_error) {
                max_error = error;
            }
        }
        ESP_LOGI(TAG, "CALIBRATION PASS: saved to NVS, max target error %.1f px", max_error);
    } else {
        ESP_LOGE(TAG, "CALIBRATION FAILED: coefficients were not saved");
    }
    gpio_set_level(backlight_pin, 0);
    ESP_LOGI(TAG, "BACKLIGHT OFF");
    return saved;
}
