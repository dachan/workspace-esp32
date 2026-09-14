#include "st7735.h"

#include <math.h>
#include <string.h>

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
#define TFT_SPI_HZ (80 * 1000 * 1000)
#define TFT_TRANSFER_ROWS 40
#define FLUID_SCALE 2
#define FLUID_FIELD_WIDTH (TFT_WIDTH / FLUID_SCALE)
#define FLUID_FIELD_HEIGHT (TFT_HEIGHT / FLUID_SCALE)
#define FLUID_TABLE_SIZE 256
#define FLUID_BRIGHTNESS_MIN 140
#define FLUID_BRIGHTNESS_RANGE 70
#define FLUID_BRIGHTNESS_STEPS (FLUID_BRIGHTNESS_RANGE + 1)
#define FLUID_HUE_SUM_MAX (3 * (FLUID_TABLE_SIZE - 1))
#define FPS_GLYPH_WIDTH 3
#define FPS_GLYPH_HEIGHT 5
#define FPS_GLYPH_SCALE 2
#define FPS_GLYPH_SPACING 2

static spi_device_handle_t s_spi;
static uint16_t *s_framebuffer;
static uint8_t s_fluid_sine[FLUID_TABLE_SIZE];
static uint8_t s_fluid_average[FLUID_HUE_SUM_MAX + 1];
static uint8_t s_fluid_brightness_index[FLUID_TABLE_SIZE];
static uint16_t s_fluid_palette[FLUID_BRIGHTNESS_STEPS][FLUID_TABLE_SIZE];
static uint16_t s_fluid_field[FLUID_FIELD_WIDTH * FLUID_FIELD_HEIGHT];
static uint16_t s_blur_previous[FLUID_FIELD_WIDTH];
static uint16_t s_blur_current[FLUID_FIELD_WIDTH];
static uint16_t s_blur_next[FLUID_FIELD_WIDTH];
static bool s_fluid_sine_ready;
static bool s_fluid_palette_ready;
static TickType_t s_fps_window_start;
static uint16_t s_frames_since_fps_update;
static uint8_t s_frames_per_second;
static TickType_t s_last_color_ticks;
static TickType_t s_last_blur_ticks;
static TickType_t s_last_transfer_ticks;

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

static uint8_t pastel_channel(uint8_t value, uint8_t brightness)
{
    const uint8_t white_mix = 185;
    uint8_t shaded = scale_channel(value, brightness);
    return white_mix + ((uint16_t)(255 - white_mix) * shaded) / 255;
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
        red = 255 - position * 3;
        green = position * 3;
        blue = 0;
    } else if (position < 170) {
        position -= 85;
        red = 0;
        green = 255 - position * 3;
        blue = position * 3;
    } else {
        position -= 170;
        red = position * 3;
        green = 0;
        blue = 255 - position * 3;
    }
    red = pastel_channel(red, brightness);
    green = pastel_channel(green, brightness);
    blue = pastel_channel(blue, brightness);
    return ((uint16_t)(red & 0xf8) << 8) |
           ((uint16_t)(green & 0xfc) << 3) |
           (blue >> 3);
}

static void initialize_fluid_palette(void)
{
    if (s_fluid_palette_ready) {
        return;
    }
    for (int value = 0; value < FLUID_TABLE_SIZE; ++value) {
        s_fluid_brightness_index[value] = (uint16_t)value * FLUID_BRIGHTNESS_RANGE / 255;
    }
    for (int sum = 0; sum <= FLUID_HUE_SUM_MAX; ++sum) {
        s_fluid_average[sum] = sum / 3;
    }
    for (int brightness = 0; brightness < FLUID_BRIGHTNESS_STEPS; ++brightness) {
        for (int hue = 0; hue < FLUID_TABLE_SIZE; ++hue) {
            s_fluid_palette[brightness][hue] =
                rainbow_color(hue, FLUID_BRIGHTNESS_MIN + brightness);
        }
    }
    s_fluid_palette_ready = true;
}

static uint16_t fluid_color(int x, int y, uint8_t phase)
{
    uint8_t horizontal = s_fluid_sine[(uint8_t)(x * 2 + phase * 2)];
    uint8_t vertical = s_fluid_sine[(uint8_t)(y * 2 - phase)];
    uint8_t diagonal = s_fluid_sine[(uint8_t)(x + y + phase * 3)];
    uint8_t swirl = s_fluid_sine[(uint8_t)(x - y + phase * 2)];
    uint8_t hue = s_fluid_average[horizontal + vertical + diagonal] + phase;
    return s_fluid_palette[s_fluid_brightness_index[swirl]][hue];
}

static uint16_t blur_three_pixels(uint16_t first, uint16_t second, uint16_t third)
{
    uint32_t red = ((first >> 11) & 0x1f) + 2 * ((second >> 11) & 0x1f) +
                   ((third >> 11) & 0x1f);
    uint32_t green = ((first >> 5) & 0x3f) + 2 * ((second >> 5) & 0x3f) +
                     ((third >> 5) & 0x3f);
    uint32_t blue = (first & 0x1f) + 2 * (second & 0x1f) + (third & 0x1f);
    return ((red / 4) << 11) | ((green / 4) << 5) | (blue / 4);
}

static void horizontal_gaussian_blur(const uint16_t *source, uint16_t *destination,
                                     int width)
{
    destination[0] = source[0];
    for (int x = 1; x < width - 1; ++x) {
        destination[x] = blur_three_pixels(source[x - 1], source[x], source[x + 1]);
    }
    destination[width - 1] = source[width - 1];
}

static void apply_gaussian_blur(void)
{
    uint16_t *upper = s_blur_previous;
    uint16_t *middle = s_blur_current;
    uint16_t *lower = s_blur_next;
    horizontal_gaussian_blur(s_fluid_field, upper, FLUID_FIELD_WIDTH);
    horizontal_gaussian_blur(&s_fluid_field[FLUID_FIELD_WIDTH], middle, FLUID_FIELD_WIDTH);
    horizontal_gaussian_blur(&s_fluid_field[2 * FLUID_FIELD_WIDTH], lower, FLUID_FIELD_WIDTH);

    for (int y = 1; y < FLUID_FIELD_HEIGHT - 1; ++y) {
        uint16_t *destination = &s_fluid_field[y * FLUID_FIELD_WIDTH];
        for (int x = 1; x < FLUID_FIELD_WIDTH - 1; ++x) {
            destination[x] = blur_three_pixels(upper[x], middle[x], lower[x]);
        }
        if (y + 2 < FLUID_FIELD_HEIGHT) {
            horizontal_gaussian_blur(&s_fluid_field[(y + 2) * FLUID_FIELD_WIDTH], upper,
                                     FLUID_FIELD_WIDTH);
        }
        uint16_t *recycled = upper;
        upper = middle;
        middle = lower;
        lower = recycled;
    }
}

static void upscale_fluid_field(void)
{
    for (int y = 0; y < FLUID_FIELD_HEIGHT; ++y) {
        const uint16_t *source = &s_fluid_field[y * FLUID_FIELD_WIDTH];
        uint16_t *destination = &s_framebuffer[y * FLUID_SCALE * TFT_WIDTH];
        for (int x = 0; x < FLUID_FIELD_WIDTH; ++x) {
            uint16_t color = source[x];
            int destination_x = x * FLUID_SCALE;
            destination[destination_x] = color;
            destination[destination_x + 1] = color;
        }
        memcpy(destination + TFT_WIDTH, destination, TFT_WIDTH * sizeof(*destination));
    }
}

typedef struct {
    char character;
    uint8_t rows[FPS_GLYPH_HEIGHT];
} fps_glyph_t;

static const fps_glyph_t s_fps_glyphs[] = {
    {'F', {0x7, 0x4, 0x6, 0x4, 0x4}},
    {'P', {0x6, 0x5, 0x6, 0x4, 0x4}},
    {'S', {0x3, 0x4, 0x2, 0x1, 0x6}},
    {'0', {0x7, 0x5, 0x5, 0x5, 0x7}},
    {'1', {0x2, 0x6, 0x2, 0x2, 0x7}},
    {'2', {0x6, 0x1, 0x2, 0x4, 0x7}},
    {'3', {0x6, 0x1, 0x2, 0x1, 0x6}},
    {'4', {0x5, 0x5, 0x7, 0x1, 0x1}},
    {'5', {0x7, 0x4, 0x6, 0x1, 0x6}},
    {'6', {0x3, 0x4, 0x7, 0x5, 0x7}},
    {'7', {0x7, 0x1, 0x2, 0x2, 0x2}},
    {'8', {0x7, 0x5, 0x7, 0x5, 0x7}},
    {'9', {0x7, 0x5, 0x7, 0x1, 0x6}},
};

static const uint8_t *fps_glyph_rows(char character)
{
    for (size_t index = 0; index < sizeof(s_fps_glyphs) / sizeof(s_fps_glyphs[0]); ++index) {
        if (s_fps_glyphs[index].character == character) {
            return s_fps_glyphs[index].rows;
        }
    }
    return NULL;
}

static uint16_t dim_color(uint16_t color)
{
    return (color >> 1) & 0x7bef;
}

static void draw_fps_glyph(char character, int origin_x, int origin_y)
{
    const uint8_t *rows = fps_glyph_rows(character);
    if (rows == NULL) {
        return;
    }
    for (int row = 0; row < FPS_GLYPH_HEIGHT; ++row) {
        for (int column = 0; column < FPS_GLYPH_WIDTH; ++column) {
            if ((rows[row] & (1 << (FPS_GLYPH_WIDTH - column - 1))) == 0) {
                continue;
            }
            for (int y = 0; y < FPS_GLYPH_SCALE; ++y) {
                for (int x = 0; x < FPS_GLYPH_SCALE; ++x) {
                    int pixel_x = origin_x + column * FPS_GLYPH_SCALE + x;
                    int pixel_y = origin_y + row * FPS_GLYPH_SCALE + y;
                    s_framebuffer[pixel_y * TFT_WIDTH + pixel_x] = 0xffff;
                }
            }
        }
    }
}

static void draw_fps_counter(void)
{
    enum {
        text_length = 6,
        glyph_advance = FPS_GLYPH_WIDTH * FPS_GLYPH_SCALE + FPS_GLYPH_SPACING,
        panel_width = text_length * glyph_advance - FPS_GLYPH_SPACING + 4,
        panel_height = FPS_GLYPH_HEIGHT * FPS_GLYPH_SCALE + 4,
        panel_x = TFT_WIDTH - panel_width - 2,
        panel_y = TFT_HEIGHT - panel_height - 2,
    };
    uint8_t displayed_fps = s_frames_per_second > 99 ? 99 : s_frames_per_second;
    char text[] = {'F', 'P', 'S', ' ', '0' + displayed_fps / 10,
                   '0' + displayed_fps % 10};

    for (int y = panel_y; y < panel_y + panel_height; ++y) {
        for (int x = panel_x; x < panel_x + panel_width; ++x) {
            int pixel_index = y * TFT_WIDTH + x;
            s_framebuffer[pixel_index] = dim_color(dim_color(s_framebuffer[pixel_index]));
        }
    }
    for (int index = 0; index < text_length; ++index) {
        draw_fps_glyph(text[index], panel_x + 2 + index * glyph_advance, panel_y + 2);
    }
}

static void update_fps_counter(void)
{
    TickType_t now = xTaskGetTickCount();
    if (s_fps_window_start == 0) {
        s_fps_window_start = now;
    }
    ++s_frames_since_fps_update;
    TickType_t elapsed_ticks = now - s_fps_window_start;
    if (elapsed_ticks >= pdMS_TO_TICKS(1000)) {
        s_frames_per_second = ((uint32_t)s_frames_since_fps_update * configTICK_RATE_HZ) /
                              elapsed_ticks;
        ESP_LOGI(TAG, "screensaver FPS: %u (color=%u ms blur=%u ms transfer=%u ms)",
                 (unsigned)s_frames_per_second,
                 (unsigned)pdTICKS_TO_MS(s_last_color_ticks),
                 (unsigned)pdTICKS_TO_MS(s_last_blur_ticks),
                 (unsigned)pdTICKS_TO_MS(s_last_transfer_ticks));
        s_frames_since_fps_update = 0;
        s_fps_window_start = now;
    }
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
    initialize_fluid_palette();
    TickType_t color_start = xTaskGetTickCount();
    for (int y = 0; y < FLUID_FIELD_HEIGHT; ++y) {
        for (int x = 0; x < FLUID_FIELD_WIDTH; ++x) {
            s_fluid_field[y * FLUID_FIELD_WIDTH + x] =
                fluid_color(x * FLUID_SCALE, y * FLUID_SCALE, phase);
        }
    }
    TickType_t blur_start = xTaskGetTickCount();
    apply_gaussian_blur();
    upscale_fluid_field();
    TickType_t transfer_start = xTaskGetTickCount();
    draw_fps_counter();
    esp_err_t err = flush_framebuffer();
    TickType_t transfer_end = xTaskGetTickCount();
    s_last_color_ticks = blur_start - color_start;
    s_last_blur_ticks = transfer_start - blur_start;
    s_last_transfer_ticks = transfer_end - transfer_start;
    if (err == ESP_OK) {
        update_fps_counter();
    }
    return err;
}
