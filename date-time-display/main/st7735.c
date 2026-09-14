#include "st7735.h"

#include <math.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7735";

#define TFT_WIDTH 240
#define TFT_HEIGHT 240
#define TFT_HOST SPI2_HOST
#define TFT_PIN_RST 8
#define TFT_PIN_DC 10
#define TFT_PIN_MOSI 11
#define TFT_PIN_SCK 12
#define TFT_SPI_HZ (26 * 1000 * 1000)
#define TFT_TRANSFER_ROWS 16
#define FLUID_TABLE_SIZE 256

static spi_device_handle_t s_spi;
static uint16_t *s_framebuffer;
static uint8_t s_fluid_sine[FLUID_TABLE_SIZE];
static bool s_fluid_sine_ready;

static esp_err_t write_bytes(const void *data, size_t length, bool command)
{
    gpio_set_level(TFT_PIN_DC, command ? 0 : 1);
    spi_transaction_t transaction = {
        .length = length * 8,
        .tx_buffer = data,
    };
    return spi_device_transmit(s_spi, &transaction);
}

static esp_err_t write_command(uint8_t command)
{
    return write_bytes(&command, 1, true);
}

static esp_err_t write_command_data(uint8_t command, const void *data, size_t length)
{
    esp_err_t err = write_command(command);
    if (err != ESP_OK) {
        return err;
    }
    return write_bytes(data, length, false);
}

static esp_err_t set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t column[] = {x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff};
    uint8_t row[] = {y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff};
    esp_err_t err = write_command_data(0x2a, column, sizeof(column));
    if (err != ESP_OK) {
        return err;
    }
    err = write_command_data(0x2b, row, sizeof(row));
    if (err != ESP_OK) {
        return err;
    }
    return write_command(0x2c);
}

static uint8_t scale_channel(uint8_t value, uint8_t brightness)
{
    return ((uint16_t)value * brightness) / 255;
}

static void initialize_fluid_sine(void)
{
    if (s_fluid_sine_ready) {
        return;
    }
    for (int index = 0; index < FLUID_TABLE_SIZE; ++index) {
        float angle = (float)index * 6.283185307f / FLUID_TABLE_SIZE;
        s_fluid_sine[index] = (uint8_t)((sinf(angle) + 1.0f) * 127.5f);
    }
    s_fluid_sine_ready = true;
}

static uint16_t rainbow_color(uint8_t position, uint8_t brightness)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    if (position < 85) {
        red = scale_channel(255 - position * 3, brightness);
        green = scale_channel(position * 3, brightness);
        blue = 0;
    } else if (position < 170) {
        position -= 85;
        red = 0;
        green = scale_channel(255 - position * 3, brightness);
        blue = scale_channel(position * 3, brightness);
    } else {
        position -= 170;
        red = scale_channel(position * 3, brightness);
        green = 0;
        blue = scale_channel(255 - position * 3, brightness);
    }
    return ((uint16_t)(red & 0xf8) << 8) |
           ((uint16_t)(green & 0xfc) << 3) |
           (blue >> 3);
}

static uint16_t fluid_color(int x, int y, uint8_t phase)
{
    uint8_t horizontal = s_fluid_sine[(uint8_t)(x * 2 + phase * 2)];
    uint8_t vertical = s_fluid_sine[(uint8_t)(y * 2 - phase)];
    uint8_t diagonal = s_fluid_sine[(uint8_t)(x + y + phase * 3)];
    uint8_t swirl = s_fluid_sine[(uint8_t)(x - y + phase * 2)];
    uint8_t hue = (uint8_t)(((uint16_t)horizontal + vertical + diagonal) / 3 + phase);
    uint8_t brightness = 64 + ((uint16_t)swirl * 191 / 255);
    return rainbow_color(hue, brightness);
}

static esp_err_t flush_framebuffer(void)
{
    ESP_RETURN_ON_ERROR(set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1), TAG,
                        "set address window");
    gpio_set_level(TFT_PIN_DC, 1);
    for (int row = 0; row < TFT_HEIGHT; row += TFT_TRANSFER_ROWS) {
        int rows = TFT_HEIGHT - row;
        if (rows > TFT_TRANSFER_ROWS) {
            rows = TFT_TRANSFER_ROWS;
        }
        spi_transaction_t transaction = {
            .length = (size_t)TFT_WIDTH * rows * 16,
            .tx_buffer = &s_framebuffer[row * TFT_WIDTH],
        };
        esp_err_t err = spi_device_transmit(s_spi, &transaction);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t st7735_init(void)
{
    gpio_config_t gpio = {
        .pin_bit_mask = (1ULL << TFT_PIN_RST) | (1ULL << TFT_PIN_DC),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio);
    if (err != ESP_OK) {
        return err;
    }

    spi_bus_config_t bus = {
        .sclk_io_num = TFT_PIN_SCK,
        .mosi_io_num = TFT_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_TRANSFER_ROWS * 2,
    };
    err = spi_bus_initialize(TFT_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t device = {
        .clock_speed_hz = TFT_SPI_HZ,
        /* The no-CS 7-pin ST7789 variant samples SPI in mode 3. */
        .mode = 3,
        /* This 7-pin ST7789 module has no exposed CS; it is hard-wired active. */
        .spics_io_num = -1,
        .queue_size = 1,
    };
    err = spi_bus_add_device(TFT_HOST, &device, &s_spi);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t color_mode = 0x55;
    uint8_t madctl = 0x00;
    ESP_RETURN_ON_ERROR(write_command(0x01), TAG, "software reset");
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_RETURN_ON_ERROR(write_command(0x11), TAG, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(write_command_data(0x3a, &color_mode, 1), TAG, "color mode");
    ESP_RETURN_ON_ERROR(write_command_data(0x36, &madctl, 1), TAG, "madctl");
    ESP_RETURN_ON_ERROR(write_command(0x21), TAG, "display inversion");
    ESP_RETURN_ON_ERROR(write_command(0x13), TAG, "normal mode");
    ESP_RETURN_ON_ERROR(write_command(0x29), TAG, "display on");
    vTaskDelay(pdMS_TO_TICKS(20));

    s_framebuffer = heap_caps_calloc(TFT_WIDTH * TFT_HEIGHT, sizeof(uint16_t),
                                     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_framebuffer == NULL) {
        ESP_LOGE(TAG, "framebuffer allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ST7789 %dx%d ready; SPI RES/DC/SDA/SCK GPIO%d/%d/%d/%d",
             TFT_WIDTH, TFT_HEIGHT, TFT_PIN_RST, TFT_PIN_DC, TFT_PIN_MOSI,
             TFT_PIN_SCK);
    return ESP_OK;
}

esp_err_t st7735_render_screensaver(uint8_t phase)
{
    if (s_framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    initialize_fluid_sine();
    for (int y = 0; y < TFT_HEIGHT; ++y) {
        for (int x = 0; x < TFT_WIDTH; ++x) {
            s_framebuffer[y * TFT_WIDTH + x] = fluid_color(x, y, phase);
        }
    }
    return flush_framebuffer();
}
