#include "display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#if defined(AI_MODEL_PROFILE_SUPERMINI)
#include "esp_lcd_gc9a01.h"
#else
#include "esp_lcd_st7796.h"
#endif
#include "esp_log.h"
#include "esp_lcd_io_spi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "canvas.h"

static const char *TAG = "display";

#define LCD_HOST      SPI2_HOST

#if defined(AI_MODEL_PROFILE_SUPERMINI)
/* ESP32S3SuperMini + 1.28" 240x240 GC9A01. The round module has no MISO
 * or separately controlled backlight pin; its VCC/LED is a 3.3 V load. */
#define PIN_MOSI   8
#define PIN_DC     9
#define PIN_RST    10
#define PIN_CS     11
#define PIN_SCK    12
#define LCD_PIXEL_CLK (26 * 1000 * 1000)
#else
/* Lonely Binary / radar-class ESP32-S3 + 3.5" TFT SPI 480x320 v1 (ST7796U). */
#define PIN_SD_CS  4
#define PIN_MOSI   8
#define PIN_DC     9
#define PIN_RST   10
#define PIN_CS    11
#define PIN_MISO  16
#define PIN_BL    17
#define PIN_SCK   18

/* 40 MHz can tear/flicker on some 3.5" SPI panels; 26 MHz is stable here. */
#define LCD_PIXEL_CLK (26 * 1000 * 1000)
#endif

#if !defined(AI_MODEL_PROFILE_SUPERMINI)
#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0
#define BL_RES     LEDC_TIMER_10_BIT
#define BL_MAX     ((1 << 10) - 1)
#endif

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb;
/* Internal-RAM bounce strips for SPI DMA (PSRAM DMA snowed first rows). */
#define FLUSH_BAND_H 16
static uint16_t *s_band;
static SemaphoreHandle_t s_xfer_done;
static bool s_flush_pending;

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    BaseType_t hp = pdFALSE;
    if (s_xfer_done) {
        xSemaphoreGiveFromISR(s_xfer_done, &hp);
    }
    return hp == pdTRUE;
}

static esp_err_t wait_flush_done(void)
{
    if (!s_flush_pending) {
        return ESP_OK;
    }
    if (xSemaphoreTake(s_xfer_done, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGE(TAG, "SPI transfer timed out; retaining DMA buffer ownership");
        return ESP_ERR_TIMEOUT;
    }
    s_flush_pending = false;
    return ESP_OK;
}

static esp_err_t backlight_init(void)
{
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    /* The round module has no BL header; its backlight is enabled by VCC. */
    return ESP_OK;
#else
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_TIMER,
        .duty_resolution = BL_RES,
        .freq_hz = 5000,
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
#endif
}

esp_err_t display_set_backlight(uint8_t percent)
{
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    (void)percent;
    return ESP_OK;
#else
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (uint32_t)BL_MAX * percent / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty), TAG, "duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
#endif
}

esp_err_t display_init(void)
{
    const size_t fb_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);

#if !defined(AI_MODEL_PROFILE_SUPERMINI)
    gpio_config_t sd_cs_cfg = {
        .pin_bit_mask = 1ULL << PIN_SD_CS,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&sd_cs_cfg), TAG, "SD CS GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_SD_CS, 1), TAG, "deselect SD");
#endif

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
#if defined(AI_MODEL_PROFILE_SUPERMINI)
        .miso_io_num = -1,
#else
        .miso_io_num = PIN_MISO,
#endif
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)fb_bytes + 8,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    s_io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PIXEL_CLK,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io),
        TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(s_io, &panel_cfg, &s_panel), TAG, "gc9a01");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(s_io, &panel_cfg, &s_panel), TAG, "st7796");
#endif

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    /* The GC9A01 module is used in its native square orientation. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), TAG, "mirror");
#else
    /* Desk pose lock (verified): MADCTL swap_xy + mirror(true,true), plus a
     * separate soft-180 DMA buffer in display_flush (never reverse s_fb in place). */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, true), TAG, "mirror");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    s_xfer_done = xSemaphoreCreateBinary();
    if (s_xfer_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    const size_t band_bytes = (size_t)DISPLAY_WIDTH * FLUSH_BAND_H * sizeof(uint16_t);
    s_band = heap_caps_malloc(band_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_fb == NULL || s_band == NULL) {
        ESP_LOGE(TAG, "framebuffer/band alloc failed (fb=%zu band=%zu)", fb_bytes, band_bytes);
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb, 0, fb_bytes);
    memset(s_band, 0, band_bytes);
    canvas_set_framebuffer(s_fb);
    ESP_LOGI(TAG, "%s %dx%d framebuffer: %zu KB PSRAM at %p",
#if defined(AI_MODEL_PROFILE_SUPERMINI)
             "GC9A01", DISPLAY_WIDTH, DISPLAY_HEIGHT,
#else
             "ST7796", DISPLAY_WIDTH, DISPLAY_HEIGHT,
#endif
             fb_bytes / 1024, s_fb);

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    return ESP_OK;
}

esp_err_t display_flush(void)
{
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    /* GC9A01 is native 240x240. Copy strips to internal DMA RAM so the
     * PSRAM framebuffer is never owned by the SPI DMA engine. */
#else
    /* Desk pose still needs soft 180 on top of MADCTL. Blit through an
     * internal-RAM band so SPI DMA never reads PSRAM (that snowed an edge). */
#endif
    const int w = DISPLAY_WIDTH;
    const int h = DISPLAY_HEIGHT;
    esp_err_t err = ESP_OK;

    for (int y = 0; y < h; y += FLUSH_BAND_H) {
        const int band_h = (y + FLUSH_BAND_H <= h) ? FLUSH_BAND_H : (h - y);
        /* Wait before filling — s_band is still owned by SPI until done. */
        ESP_RETURN_ON_ERROR(wait_flush_done(), TAG, "wait for SPI band");
        for (int row = 0; row < band_h; row++) {
            const int panel_y = y + row;
#if defined(AI_MODEL_PROFILE_SUPERMINI)
            const int src_y = panel_y;
#else
            const int src_y = h - 1 - panel_y;
#endif
            const uint16_t *src = s_fb + (size_t)src_y * (size_t)w;
            uint16_t *dst = s_band + (size_t)row * (size_t)w;
#if defined(AI_MODEL_PROFILE_SUPERMINI)
            memcpy(dst, src, (size_t)w * sizeof(*dst));
#else
            for (int x = 0; x < w; x++) {
                dst[x] = src[w - 1 - x];
            }
#endif
        }
        s_flush_pending = true;
        err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, w, y + band_h, s_band);
        if (err != ESP_OK) {
            s_flush_pending = false;
            return err;
        }
    }
    return wait_flush_done();
}
