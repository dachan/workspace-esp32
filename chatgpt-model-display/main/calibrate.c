#include "calibrate.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "display.h"
#include "esp_log.h"
#include "font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "touch.h"

#define CAL_NS "touch"
#define CAL_KEY "c35_desk"
#define TARGET_COUNT 5
#define SAMPLE_COUNT 10

typedef struct {
    float x_offset;
    float x_from_raw_x;
    float x_from_raw_y;
    float y_offset;
    float y_from_raw_x;
    float y_from_raw_y;
} cal_t;

static const char *TAG = "calibrate";
static const uint16_t s_targets[TARGET_COUNT][2] = {
    {40, 40}, {439, 40}, {439, 279}, {40, 279}, {240, 160},
};

static cal_t s_cal;
static bool s_have;

static void fallback_map(uint16_t raw_x, uint16_t raw_y, int *x, int *y)
{
    *x = raw_y;
    *y = raw_x;
}

static void clamp_ui(int *x, int *y)
{
    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
    }
    if (*x >= DISPLAY_WIDTH) {
        *x = DISPLAY_WIDTH - 1;
    }
    if (*y >= DISPLAY_HEIGHT) {
        *y = DISPLAY_HEIGHT - 1;
    }
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
        result[row] = (float)matrix[row][3];
    }
    return true;
}

static bool fit(const uint16_t raw[][2], cal_t *out)
{
    double normal[3][3] = {{0}};
    double target_x[3] = {0};
    double target_y[3] = {0};
    for (int i = 0; i < TARGET_COUNT; ++i) {
        double rawv[3] = {1.0, raw[i][0], raw[i][1]};
        for (int row = 0; row < 3; ++row) {
            target_x[row] += rawv[row] * s_targets[i][0];
            target_y[row] += rawv[row] * s_targets[i][1];
            for (int column = 0; column < 3; ++column) {
                normal[row][column] += rawv[row] * rawv[column];
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
    float xc[3];
    float yc[3];
    if (!solve_3x3(x_matrix, xc) || !solve_3x3(y_matrix, yc)) {
        return false;
    }
    out->x_offset = xc[0];
    out->x_from_raw_x = xc[1];
    out->x_from_raw_y = xc[2];
    out->y_offset = yc[0];
    out->y_from_raw_x = yc[1];
    out->y_from_raw_y = yc[2];
    return isfinite(out->x_offset) && isfinite(out->y_offset);
}

static bool save_cal(const cal_t *cal)
{
    nvs_handle_t h;
    if (nvs_open(CAL_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, CAL_KEY, cal, sizeof(*cal));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static void draw_target(int index)
{
    const uint16_t bg = display_rgb(12, 14, 22);
    const uint16_t fg = display_rgb(240, 244, 250);
    const uint16_t accent = display_rgb(88, 166, 255);
    const int tx = s_targets[index][0];
    const int ty = s_targets[index][1];
    display_fill(bg);
    display_fill_rect(tx - 16, ty - 2, 33, 5, fg);
    display_fill_rect(tx - 2, ty - 16, 5, 33, fg);
    display_fill_rect(tx - 5, ty - 5, 11, 11, accent);
    char label[16];
    snprintf(label, sizeof(label), "TAP %u/%u", (unsigned)(index + 1), (unsigned)TARGET_COUNT);
    font_draw_text(20, 20, label, accent, bg, 2);
    display_flush();
}

static void wait_release(void)
{
    uint16_t x;
    uint16_t y;
    while (touch_raw(&x, &y)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(120));
}

static bool sample_point(uint16_t *raw_x, uint16_t *raw_y)
{
    wait_release();
    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    unsigned samples = 0;
    bool down = false;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
    while (xTaskGetTickCount() < deadline) {
        uint16_t x;
        uint16_t y;
        if (touch_raw(&x, &y)) {
            if (!down) {
                down = true;
                sum_x = 0;
                sum_y = 0;
                samples = 0;
            }
            sum_x += x;
            sum_y += y;
            samples++;
        } else if (down) {
            vTaskDelay(pdMS_TO_TICKS(120));
            if (samples >= SAMPLE_COUNT) {
                *raw_x = (uint16_t)(sum_x / samples);
                *raw_y = (uint16_t)(sum_y / samples);
                return true;
            }
            down = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

esp_err_t calibrate_init(void)
{
    nvs_handle_t h;
    if (nvs_open(CAL_NS, NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK;
    }
    size_t len = sizeof(s_cal);
    esp_err_t err = nvs_get_blob(h, CAL_KEY, &s_cal, &len);
    nvs_close(h);
    if (err == ESP_OK && len == sizeof(s_cal) && isfinite(s_cal.x_offset)
        && isfinite(s_cal.x_from_raw_x) && isfinite(s_cal.y_offset)) {
        s_have = true;
        ESP_LOGI(TAG, "loaded desk touch calibration");
    }
    return ESP_OK;
}

bool calibrate_map(uint16_t raw_x, uint16_t raw_y, int *x, int *y)
{
    if (!x || !y) {
        return false;
    }
    if (!s_have) {
        fallback_map(raw_x, raw_y, x, y);
        clamp_ui(x, y);
        return true;
    }
    float fx = s_cal.x_offset + s_cal.x_from_raw_x * raw_x + s_cal.x_from_raw_y * raw_y;
    float fy = s_cal.y_offset + s_cal.y_from_raw_x * raw_x + s_cal.y_from_raw_y * raw_y;
    *x = (int)lroundf(fx);
    *y = (int)lroundf(fy);
    clamp_ui(x, y);
    return true;
}

bool calibrate_run(void)
{
    ESP_LOGI(TAG, "calibration start");
    uint16_t raw[TARGET_COUNT][2];
    for (int i = 0; i < TARGET_COUNT; ++i) {
        draw_target(i);
        if (!sample_point(&raw[i][0], &raw[i][1])) {
            ESP_LOGE(TAG, "calibration timeout at target %d", i + 1);
            return false;
        }
        ESP_LOGI(TAG, "target %d raw=(%u,%u)", i + 1, raw[i][0], raw[i][1]);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    cal_t fitted;
    if (!fit(raw, &fitted) || !save_cal(&fitted)) {
        ESP_LOGE(TAG, "calibration fit/save failed");
        return false;
    }
    s_cal = fitted;
    s_have = true;
    ESP_LOGI(TAG, "calibration saved");
    wait_release();
    return true;
}
