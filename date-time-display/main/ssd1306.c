#include "ssd1306.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "font5x7.h"

static const char *TAG = "ssd1306";

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)
#define OLED_ADDRESS 0x3c
#define OLED_PORT I2C_NUM_0
#define OLED_PIN_SDA 6
#define OLED_PIN_SCL 7
#define OLED_I2C_HZ 400000

static uint8_t s_buffer[OLED_WIDTH * OLED_PAGES];

static esp_err_t command(uint8_t value)
{
    uint8_t data[] = {0x00, value};
    return i2c_master_write_to_device(OLED_PORT, OLED_ADDRESS, data, sizeof(data),
                                      pdMS_TO_TICKS(100));
}

static esp_err_t commands(const uint8_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = command(values[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static void clear_buffer(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

static void draw_text(const char *text, int x, int y, int scale)
{
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        const uint8_t *glyph = date_time_glyph(*cursor);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((glyph[column] & (1u << row)) == 0) {
                    continue;
                }
                for (int dx = 0; dx < scale; ++dx) {
                    for (int dy = 0; dy < scale; ++dy) {
                        int px = x + column * scale + dx;
                        int py = y + row * scale + dy;
                        if (px < 0 || px >= OLED_WIDTH || py < 0 || py >= OLED_HEIGHT) {
                            continue;
                        }
                        s_buffer[(py / 8) * OLED_WIDTH + px] |= (uint8_t)(1u << (py % 8));
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

static esp_err_t refresh(void)
{
    uint8_t page_data[1 + OLED_WIDTH];
    page_data[0] = 0x40;
    for (int page = 0; page < OLED_PAGES; ++page) {
        esp_err_t err = command((uint8_t)(0xb0 | page));
        if (err != ESP_OK) {
            return err;
        }
        err = command(0x00);
        if (err != ESP_OK) {
            return err;
        }
        err = command(0x10);
        if (err != ESP_OK) {
            return err;
        }
        memcpy(&page_data[1], &s_buffer[page * OLED_WIDTH], OLED_WIDTH);
        err = i2c_master_write_to_device(OLED_PORT, OLED_ADDRESS, page_data,
                                         sizeof(page_data), pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ssd1306_init(void)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_PIN_SDA,
        .scl_io_num = OLED_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_HZ,
    };
    esp_err_t err = i2c_param_config(OLED_PORT, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(OLED_PORT, config.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    static const uint8_t init_values[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12,
        0x81, 0xcf, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf,
    };
    ESP_RETURN_ON_ERROR(commands(init_values, sizeof(init_values)), TAG, "init");
    clear_buffer();
    ESP_RETURN_ON_ERROR(refresh(), TAG, "clear");
    ESP_LOGI(TAG, "SSD1306 128x64 ready; I2C SDA GPIO%d, SCL GPIO%d, address 0x%02x",
             OLED_PIN_SDA, OLED_PIN_SCL, OLED_ADDRESS);
    return ESP_OK;
}

esp_err_t ssd1306_render(const char *date, const char *time_text)
{
    clear_buffer();
    draw_text(date, 2, 2, 1);
    draw_text(time_text, 2, 26, 2);
    return refresh();
}
