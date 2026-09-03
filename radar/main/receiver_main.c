#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "display_profile.h"
#include "display_sync.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "radar_link.h"
#include "radar_view.h"

enum {
    PIN_LCD_MOSI = 8,
    PIN_LCD_DC = 9,
    PIN_LCD_RST = 10,
    PIN_LCD_CS = 11,
    PIN_LCD_MISO = 16,
    PIN_BACKLIGHT = 17,
    PIN_LCD_SCLK = 18,
    LCD_TRANSFER_ROWS = 32,
    RENDER_INTERVAL_MS = 33,
    PING_INTERVAL_MS = 2000,
};

#define LCD_HOST SPI2_HOST

static const char *TAG = "receiver";
static esp_lcd_panel_handle_t s_panel;

static void init_display(void)
{
    const gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 0));

    const spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_TRANSFER_ROWS * sizeof(uint16_t) + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t panel_io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                              &io_config, &panel_io));
    ESP_ERROR_CHECK(display_sync_init(panel_io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(display_profile_create_panel(panel_io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(display_profile_apply_view_orientation(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 1));
}

static bool copy_received_people(radar_view_t *view, radar_link_frame_t *frame)
{
    if (!radar_link_receive(frame)) {
        return false;
    }

    radar_person_t people[RADAR_VIEW_MAX_PEOPLE] = {0};
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        people[index] = (radar_person_t) {
            .x_mm = frame->targets[index].x_mm,
            .y_mm = frame->targets[index].y_mm,
            .active = frame->targets[index].active,
        };
    }
    radar_view_set_people(view, people, frame->radial_acceleration_mm_per_second_squared);
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "radar display receiver start");
    init_display();
    ESP_ERROR_CHECK(radar_link_init());

    radar_view_t view;
    radar_view_start(s_panel, &view);
    radar_link_frame_t frame = {0};
    TickType_t last_render = xTaskGetTickCount() - pdMS_TO_TICKS(RENDER_INTERVAL_MS);
    TickType_t last_ping = 0;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (copy_received_people(&view, &frame)
            && now - last_ping >= pdMS_TO_TICKS(PING_INTERVAL_MS)) {
            radar_view_trigger_ping(&view);
            last_ping = now;
        }
        if (now - last_render >= pdMS_TO_TICKS(RENDER_INTERVAL_MS)) {
            radar_view_step(s_panel, &view);
            last_render = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
