#include "display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

static const char *TAG = "display";

// Pinout — see the table in AGENTS.md. GPIO10-13 are the ESP32-S3's native
// IOMUX pins for SPI2, which keeps the bus off the GPIO matrix and lets it clock
// at full speed. Touch shares SCK/MOSI/MISO and is not wired yet.
#define PIN_SCK  12
#define PIN_MOSI 11
#define PIN_MISO 13  // optional; the ILI9341 is write-only in normal use
#define PIN_CS   10
#define PIN_DC    9
#define PIN_RST  14
#define PIN_BL   21

#define LCD_HOST      SPI2_HOST
#define LCD_PIXEL_CLK (40 * 1000 * 1000)

#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0
#define BL_RES     LEDC_TIMER_10_BIT
#define BL_MAX     ((1 << 10) - 1)

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_TIMER,
        .duty_resolution = BL_RES,
        .freq_hz = 5000,  // above audible, below anything the panel cares about
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");

    ledc_channel_config_t channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .gpio_num = PIN_BL,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel);
}

esp_err_t display_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (uint32_t)BL_MAX * percent / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty), TAG, "duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

esp_err_t display_init(void)
{
    // One full frame is the largest transfer we will ever issue.
    const size_t fb_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)fb_bytes + 8,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PIXEL_CLK,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io),
        TAG, "panel io");

    // BGR rather than RGB: these ILI9341 modules almost always wire the panel
    // that way. If red and blue come out swapped, this is the line to flip.
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel), TAG, "ili9341");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");

    // The panel is natively 240x320 portrait; swapping axes gives the 320x240
    // landscape the creature is designed for.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    if (s_fb == NULL) {
        ESP_LOGE(TAG, "framebuffer alloc failed (%zu bytes in PSRAM)", fb_bytes);
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb, 0, fb_bytes);
    ESP_LOGI(TAG, "framebuffer: %zu KB in PSRAM at %p", fb_bytes / 1024, s_fb);

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    return ESP_OK;
}

uint16_t *display_framebuffer(void)
{
    return s_fb;
}

esp_err_t display_flush(void)
{
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, s_fb);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t colour)
{
    if (s_fb == NULL) {
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_WIDTH)  { w = DISPLAY_WIDTH - x; }
    if (y + h > DISPLAY_HEIGHT) { h = DISPLAY_HEIGHT - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int row = y; row < y + h; row++) {
        uint16_t *line = s_fb + (size_t)row * DISPLAY_WIDTH + x;
        for (int col = 0; col < w; col++) {
            line[col] = colour;
        }
    }
}

void display_fill(uint16_t colour)
{
    display_fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, colour);
}
