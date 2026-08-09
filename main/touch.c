#include "touch.h"

#include <stdlib.h>

#include "display.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "touch";

// Own bus — see the note in touch.h. Order matches the display header's own
// silkscreen: T_CLK, T_CS, T_DIN, T_DO, T_IRQ.
#define PIN_T_CLK 18
#define PIN_T_CS  15
#define PIN_T_DIN 17
#define PIN_T_DO   8
#define PIN_T_IRQ 16

#define TOUCH_HOST SPI3_HOST
#define TOUCH_CLK  (2 * 1000 * 1000)  // XPT2046 tolerates ~2.5MHz; stay under it

#define NVS_NAMESPACE "touch"
#define NVS_KEY_CAL   "cal"

static esp_lcd_touch_handle_t s_touch;
static touch_cal_t s_cal;
static bool s_calibrated;

esp_err_t touch_init(void)
{
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_T_CLK,
        .mosi_io_num = PIN_T_DIN,
        .miso_io_num = PIN_T_DO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(TOUCH_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_T_CS);
    io_cfg.pclk_hz = TOUCH_CLK;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_HOST, &io_cfg, &io),
        TAG, "panel io");

    // No swap or mirror here: the driver is returning raw ADC counts, so every
    // orientation decision belongs in the calibration fit instead. Doing it in
    // one place stops the two transforms fighting each other.
    esp_lcd_touch_config_t cfg = {
        .x_max = DISPLAY_WIDTH,
        .y_max = DISPLAY_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = PIN_T_IRQ,
        .flags = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(io, &cfg, &s_touch), TAG, "xpt2046");

    ESP_LOGI(TAG, "XPT2046 ready on SPI3 (CLK=%d CS=%d DIN=%d DO=%d IRQ=%d)",
             PIN_T_CLK, PIN_T_CS, PIN_T_DIN, PIN_T_DO, PIN_T_IRQ);
    return ESP_OK;
}

bool touch_is_calibrated(void)
{
    return s_calibrated;
}

// One raw sample. Returns false when the panel is not being pressed.
static bool touch_sample_raw(uint16_t *rx, uint16_t *ry)
{
    if (s_touch == NULL) {
        return false;
    }
    esp_lcd_touch_read_data(s_touch);

    esp_lcd_touch_point_data_t point = {0};
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_touch, &point, &count, 1) != ESP_OK || count == 0) {
        return false;
    }
    if (rx) { *rx = point.x; }
    if (ry) { *ry = point.y; }
    return true;
}

static int map_axis(int32_t raw, int32_t raw_lo, int32_t raw_hi,
                    int screen_lo, int screen_hi, int screen_max)
{
    int32_t span = raw_hi - raw_lo;
    if (span == 0) {
        return screen_lo;
    }
    int v = screen_lo + (int)(((int64_t)(raw - raw_lo) * (screen_hi - screen_lo)) / span);
    if (v < 0) { v = 0; }
    if (v > screen_max) { v = screen_max; }
    return v;
}

bool touch_read(int *x, int *y, uint16_t *raw_x, uint16_t *raw_y)
{
    uint16_t rx = 0, ry = 0;
    if (!touch_sample_raw(&rx, &ry)) {
        return false;
    }
    if (raw_x) { *raw_x = rx; }
    if (raw_y) { *raw_y = ry; }

    if (!s_calibrated) {
        if (x) { *x = rx; }
        if (y) { *y = ry; }
        return true;
    }

    int32_t a = s_cal.swap_xy ? ry : rx;
    int32_t b = s_cal.swap_xy ? rx : ry;
    if (x) {
        *x = map_axis(a, s_cal.x_raw_lo, s_cal.x_raw_hi,
                      TOUCH_CAL_X_LO, TOUCH_CAL_X_HI, DISPLAY_WIDTH - 1);
    }
    if (y) {
        *y = map_axis(b, s_cal.y_raw_lo, s_cal.y_raw_hi,
                      TOUCH_CAL_Y_LO, TOUCH_CAL_Y_HI, DISPLAY_HEIGHT - 1);
    }
    return true;
}

// ---- calibration ----------------------------------------------------------

static void draw_target(int cx, int cy, uint16_t colour)
{
    display_fill(display_rgb(10, 10, 14));
    display_fill_rect(cx - 14, cy - 1, 29, 3, colour);
    display_fill_rect(cx - 1, cy - 14, 3, 29, colour);
    display_fill_rect(cx - 5, cy - 5, 11, 11, display_rgb(10, 10, 14));
    display_fill_rect(cx - 2, cy - 2, 5, 5, colour);
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_flush());
}

// Waits for a firm, steady press and returns the averaged raw reading.
// Averaging matters: a single XPT2046 sample is noisy enough to shift the fit.
static bool capture_point(int cx, int cy, int32_t *out_rx, int32_t *out_ry)
{
    draw_target(cx, cy, display_rgb(255, 190, 0));

    // Wait for release first, so one long press cannot satisfy two targets.
    int idle = 0;
    while (idle < 10) {
        idle = touch_sample_raw(NULL, NULL) ? 0 : idle + 1;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    const int needed = 24;
    int64_t sum_x = 0, sum_y = 0;
    int got = 0;
    while (got < needed) {
        uint16_t rx, ry;
        if (touch_sample_raw(&rx, &ry)) {
            sum_x += rx;
            sum_y += ry;
            got++;
        } else {
            // Press released too early — discard and start this target over.
            if (got > 0) {
                got = 0;
                sum_x = sum_y = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    *out_rx = (int32_t)(sum_x / needed);
    *out_ry = (int32_t)(sum_y / needed);

    draw_target(cx, cy, display_rgb(0, 220, 90));
    ESP_LOGI(TAG, "  target (%3d,%3d) -> raw (%4ld,%4ld)",
             cx, cy, (long)*out_rx, (long)*out_ry);
    vTaskDelay(pdMS_TO_TICKS(400));
    return true;
}

esp_err_t touch_calibrate(void)
{
    ESP_LOGI(TAG, "calibration: press each crosshair firmly, one at a time");

    int32_t a_rx, a_ry, b_rx, b_ry, c_rx, c_ry;
    capture_point(TOUCH_CAL_X_LO, TOUCH_CAL_Y_LO, &a_rx, &a_ry);  // top-left
    capture_point(TOUCH_CAL_X_HI, TOUCH_CAL_Y_LO, &b_rx, &b_ry);  // top-right
    capture_point(TOUCH_CAL_X_LO, TOUCH_CAL_Y_HI, &c_rx, &c_ry);  // bottom-left

    // A->B moves only in screen X. Whichever raw axis moved more is the one
    // tracking screen X, which is how we detect a transposed panel without
    // having to guess at mirror flags.
    int32_t ab_dx = labs(b_rx - a_rx), ab_dy = labs(b_ry - a_ry);
    s_cal.swap_xy = (ab_dy > ab_dx);

    if (s_cal.swap_xy) {
        s_cal.x_raw_lo = a_ry;  s_cal.x_raw_hi = b_ry;
        s_cal.y_raw_lo = a_rx;  s_cal.y_raw_hi = c_rx;
    } else {
        s_cal.x_raw_lo = a_rx;  s_cal.x_raw_hi = b_rx;
        s_cal.y_raw_lo = a_ry;  s_cal.y_raw_hi = c_ry;
    }

    if (s_cal.x_raw_hi == s_cal.x_raw_lo || s_cal.y_raw_hi == s_cal.y_raw_lo) {
        ESP_LOGE(TAG, "calibration failed: an axis did not move between targets");
        return ESP_FAIL;
    }

    s_calibrated = true;
    ESP_LOGI(TAG, "calibration: swap_xy=%d  x_raw %ld..%ld  y_raw %ld..%ld",
             s_cal.swap_xy, (long)s_cal.x_raw_lo, (long)s_cal.x_raw_hi,
             (long)s_cal.y_raw_lo, (long)s_cal.y_raw_hi);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not open NVS (%s) — calibration is session-only",
                 esp_err_to_name(err));
        return ESP_OK;
    }
    err = nvs_set_blob(nvs, NVS_KEY_CAL, &s_cal, sizeof(s_cal));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "calibration saved — it will persist across reboots");
    } else {
        ESP_LOGW(TAG, "calibration save failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t touch_load_calibration(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs), TAG, "nvs open");

    size_t len = sizeof(s_cal);
    esp_err_t err = nvs_get_blob(nvs, NVS_KEY_CAL, &s_cal, &len);
    nvs_close(nvs);

    if (err == ESP_OK && len == sizeof(s_cal)) {
        s_calibrated = true;
        ESP_LOGI(TAG, "calibration loaded: swap_xy=%d  x_raw %ld..%ld  y_raw %ld..%ld",
                 s_cal.swap_xy, (long)s_cal.x_raw_lo, (long)s_cal.x_raw_hi,
                 (long)s_cal.y_raw_lo, (long)s_cal.y_raw_hi);
    }
    return err;
}
