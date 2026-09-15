#include "ssd1306.h"

#include <stdbool.h>
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
static uint8_t s_address = 0x3c;
static bool s_ready;

static esp_err_t command(uint8_t value)
{
    uint8_t data[] = {0x00, value};
    return i2c_master_write_to_device(OLED_PORT, s_address, data, sizeof(data),
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

static int text_width(const char *text, int scale)
{
    size_t length = strlen(text);
    return length == 0 ? 0 : (int)(length * 6 * scale - scale);
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
        err = i2c_master_write_to_device(OLED_PORT, s_address, page_data,
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
    esp_err_t last_error = ESP_FAIL;
    const uint8_t addresses[] = {0x3c, 0x3d};
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        s_address = addresses[i];
        last_error = commands(init_values, sizeof(init_values));
        if (last_error != ESP_OK) {
            continue;
        }
        clear_buffer();
        last_error = refresh();
        if (last_error == ESP_OK) {
            s_ready = true;
            ESP_LOGI(TAG, "SSD1306 128x64 ready; I2C SDA GPIO%d, SCL GPIO%d, address 0x%02x",
                     OLED_PIN_SDA, OLED_PIN_SCL, s_address);
            return ESP_OK;
        }
    }
    s_ready = false;
    ESP_LOGW(TAG, "SSD1306 not responding at 0x3C or 0x3D (%s)",
             esp_err_to_name(last_error));
    return last_error;
}

esp_err_t ssd1306_render(const char *date, const char *time_text)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const int date_scale = 1;
    const int time_scale = 2;
    const int line_gap = 6;
    const int group_height = 7 * date_scale + line_gap + 7 * time_scale;
    const int time_y = (OLED_HEIGHT - group_height) / 2;
    const int date_y = time_y + 7 * time_scale + line_gap;

    clear_buffer();
    draw_text(date, (OLED_WIDTH - text_width(date, date_scale)) / 2,
              date_y, date_scale);
    draw_text(time_text, (OLED_WIDTH - text_width(time_text, time_scale)) / 2,
              time_y, time_scale);
    return refresh();
}
