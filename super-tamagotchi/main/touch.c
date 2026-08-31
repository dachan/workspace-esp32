#include "touch.h"

#include "display.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"

static const char *TAG = "touch";

// Pin order follows the new display header: CTP_SCL, CTP_RST, CTP_SDA,
// CTP_INT. The controller is an FT6336G-family part at I2C address 0x38.
#define PIN_CTP_INT  5
#define PIN_CTP_SDA  6
#define PIN_CTP_RST  7
#define PIN_CTP_SCL 15

#define TOUCH_I2C_PORT I2C_NUM_0

static esp_lcd_touch_handle_t s_touch;
static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t touch_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = PIN_CTP_SDA,
        .scl_io_num = PIN_CTP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "I2C bus");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &io),
                        TAG, "panel io");

    // The digitizer reports native 240x320 portrait coordinates. Apply the
    // same swap/mirror combination as display_init() so callers receive the
    // creature's 320x240 landscape coordinate system directly.
    esp_lcd_touch_config_t cfg = {
        .x_max = DISPLAY_HEIGHT,
        .y_max = DISPLAY_WIDTH,
        .rst_gpio_num = PIN_CTP_RST,
        .int_gpio_num = PIN_CTP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = true, .mirror_x = true, .mirror_y = true },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &cfg, &s_touch),
                        TAG, "FT6336");

    ESP_LOGI(TAG, "FT6336 ready on I2C%d (SCL=%d SDA=%d RST=%d INT=%d, addr=0x38)",
             TOUCH_I2C_PORT, PIN_CTP_SCL, PIN_CTP_SDA, PIN_CTP_RST, PIN_CTP_INT);
    return ESP_OK;
}

bool touch_read(int *x, int *y, uint16_t *raw_x, uint16_t *raw_y)
{
    if (s_touch == NULL || esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        return false;
    }

    esp_lcd_touch_point_data_t point = {0};
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_touch, &point, &count, 1) != ESP_OK || count == 0) {
        return false;
    }

    // The touch helper mirrors using dimensions rather than last valid indices,
    // so a press at raw zero can land one pixel past the framebuffer edge.
    int tx = point.x < DISPLAY_WIDTH ? point.x : DISPLAY_WIDTH - 1;
    int ty = point.y < DISPLAY_HEIGHT ? point.y : DISPLAY_HEIGHT - 1;
    if (x) { *x = tx; }
    if (y) { *y = ty; }
    if (raw_x) { *raw_x = point.x; }
    if (raw_y) { *raw_y = point.y; }
    return true;
}
