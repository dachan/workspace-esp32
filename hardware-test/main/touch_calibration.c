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

static bool read_raw_point(touch_read_fn_t touch_read, touch_hold_fn_t touch_hold,
                           uint16_t *raw_x, uint16_t *raw_y)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
    uint16_t ignored_x;
    uint16_t ignored_y;

    while (xTaskGetTickCount() < deadline) {
        if (!touch_read(&ignored_x, &ignored_y)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    unsigned int samples = 0;
    TickType_t touch_started_at = 0;
    while (xTaskGetTickCount() < deadline) {
        uint16_t sample_x;
        uint16_t sample_y;
        if (touch_read(&sample_x, &sample_y)) {
            TickType_t now = xTaskGetTickCount();
            if (touch_started_at == 0) {
                touch_started_at = now;
            }
            sum_x += sample_x;
            sum_y += sample_y;
            ++samples;
            if (touch_hold
                    && now - touch_started_at >= pdMS_TO_TICKS(POWER_OFF_HOLD_MS)) {
                ESP_LOGI(TAG, "POWER OFF REQUESTED: three-second calibration hold");
                touch_hold();
            }
        } else if (samples >= SAMPLE_COUNT) {
            *raw_x = sum_x / samples;
            *raw_y = sum_y / samples;
            return true;
        } else {
            sum_x = 0;
            sum_y = 0;
            samples = 0;
            touch_started_at = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
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
        if (!read_raw_point(touch_read, touch_hold,
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
