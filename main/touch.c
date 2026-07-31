#include "touch.h"

#include "display.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"

static const char *TAG = "touch";

// Own bus — see the note in touch.h.
#define PIN_T_CLK 18
#define PIN_T_DIN 17
#define PIN_T_DO   8
#define PIN_T_CS  15
#define PIN_T_IRQ 16

#define TOUCH_HOST SPI3_HOST
#define TOUCH_CLK  (2 * 1000 * 1000)  // XPT2046 tolerates ~2.5MHz; stay under it

static esp_lcd_touch_handle_t s_touch;

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

    // Orientation flags mirror what the display does, so touch coordinates land
    // in the same frame as the pixels. If a press reads mirrored or transposed,
    // these three are the knobs — not the wiring.
    esp_lcd_touch_config_t cfg = {
        .x_max = DISPLAY_WIDTH,
        .y_max = DISPLAY_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = PIN_T_IRQ,
        .flags = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(io, &cfg, &s_touch), TAG, "xpt2046");

    ESP_LOGI(TAG, "XPT2046 ready on SPI3 (CLK=%d DIN=%d DO=%d CS=%d IRQ=%d)",
             PIN_T_CLK, PIN_T_DIN, PIN_T_DO, PIN_T_CS, PIN_T_IRQ);
    return ESP_OK;
}

bool touch_read(int *x, int *y, uint16_t *raw_x, uint16_t *raw_y)
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

    if (x) { *x = point.x; }
    if (y) { *y = point.y; }
    // The component already scales to x_max/y_max; keep the same values as the
    // "raw" reference until real calibration replaces this.
    if (raw_x) { *raw_x = point.x; }
    if (raw_y) { *raw_y = point.y; }
    return true;
}
