#include "st7735.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
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
#define FLUID_SCALE 3
#define FLUID_FIELD_WIDTH (TFT_WIDTH / FLUID_SCALE)
#define FLUID_FIELD_HEIGHT (TFT_HEIGHT / FLUID_SCALE)
#define FLUID_TABLE_SIZE 256
#define FLUID_TINT_COUNT 16
#define FLUID_TINT_LIGHTEN_MAX 24
#define FLUID_PALETTE_UPDATE_INTERVAL 8
#define FLUID_HUE_SUM_MAX (3 * (FLUID_TABLE_SIZE - 1))
#define FPS_GLYPH_WIDTH 3
#define FPS_GLYPH_HEIGHT 5
#define FPS_GLYPH_SCALE 2
#define FPS_GLYPH_SPACING 2

static spi_device_handle_t s_spi;
static uint16_t *s_framebuffer;
static uint16_t s_transfer_buffer[TFT_WIDTH * TFT_TRANSFER_ROWS];
static uint8_t s_fluid_sine[FLUID_TABLE_SIZE];
static uint8_t s_fluid_average[FLUID_HUE_SUM_MAX + 1];
static uint16_t s_fluid_palette[FLUID_TABLE_SIZE];
static uint16_t s_fluid_field[FLUID_FIELD_WIDTH * FLUID_FIELD_HEIGHT];
static uint16_t s_blur_previous[FLUID_FIELD_WIDTH];
static uint16_t s_blur_current[FLUID_FIELD_WIDTH];
static uint16_t s_blur_next[FLUID_FIELD_WIDTH];
static bool s_fluid_sine_ready;
static bool s_fluid_palette_ready;
static uint16_t s_fluid_palette_tick;
static uint32_t s_fluid_random_state = 0x6d2b79f5u;
static TickType_t s_fps_window_start;
static uint16_t s_frames_since_fps_update;
static uint8_t s_frames_per_second;

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

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} fluid_palette_color_t;

static fluid_palette_color_t s_fluid_base_color;
static fluid_palette_color_t s_fluid_target_color;
static uint16_t s_fluid_morph_progress;
static fluid_palette_color_t s_fluid_tints[FLUID_TINT_COUNT];

static uint16_t fluid_palette_color(uint8_t position)
{
    uint8_t tint = ((uint16_t)position * (FLUID_TINT_COUNT - 1)) / (FLUID_TABLE_SIZE - 1);
    const fluid_palette_color_t *color = &s_fluid_tints[tint];
    uint8_t red = color->red;
    uint8_t green = color->green;
    uint8_t blue = color->blue;
    return ((uint16_t)(red & 0xf8) << 8) |
           ((uint16_t)(green & 0xfc) << 3) |
           (blue >> 3);
}

static uint32_t fluid_random_next(void)
{
    uint32_t value = s_fluid_random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    s_fluid_random_state = value;
    return value;
}

static fluid_palette_color_t random_pastel_color(void)
{
    /* HSV with high value and restrained saturation keeps every target light. */
    uint16_t hue = fluid_random_next() % 360;
    uint8_t saturation = 32 + fluid_random_next() % 33;
    uint8_t value = 244 + fluid_random_next() % 12;
    uint8_t minimum = ((uint16_t)value * (255 - saturation)) / 255;
    uint8_t chroma = value - minimum;
    uint8_t offset = ((uint16_t)(hue % 60) * chroma) / 60;
    uint8_t second = ((hue / 60) & 1) == 0 ? minimum + offset : value - offset;

    switch (hue / 60) {
    case 0:
        return (fluid_palette_color_t){value, second, minimum};
    case 1:
        return (fluid_palette_color_t){second, value, minimum};
    case 2:
        return (fluid_palette_color_t){minimum, value, second};
    case 3:
        return (fluid_palette_color_t){minimum, second, value};
    case 4:
        return (fluid_palette_color_t){second, minimum, value};
    default:
        return (fluid_palette_color_t){value, minimum, second};
    }
}

static fluid_palette_color_t blend_pastel_colors(uint16_t progress)
{
    return (fluid_palette_color_t){
        .red = ((uint32_t)s_fluid_base_color.red * (255 - progress) +
                (uint32_t)s_fluid_target_color.red * progress + 127) / 255,
        .green = ((uint32_t)s_fluid_base_color.green * (255 - progress) +
                  (uint32_t)s_fluid_target_color.green * progress + 127) / 255,
        .blue = ((uint32_t)s_fluid_base_color.blue * (255 - progress) +
                 (uint32_t)s_fluid_target_color.blue * progress + 127) / 255,
    };
}

static void update_fluid_palette(void)
{
    if (!s_fluid_palette_ready) {
        s_fluid_random_state ^= esp_random();
        if (s_fluid_random_state == 0) {
            s_fluid_random_state = 0x6d2b79f5u;
        }
        s_fluid_base_color = random_pastel_color();
        s_fluid_target_color = random_pastel_color();
        s_fluid_morph_progress = 0;
        for (int sum = 0; sum <= FLUID_HUE_SUM_MAX; ++sum) {
            s_fluid_average[sum] = sum / 3;
        }
        s_fluid_palette_ready = true;
    } else if (s_fluid_morph_progress > 255) {
        s_fluid_base_color = s_fluid_target_color;
        s_fluid_target_color = random_pastel_color();
        s_fluid_morph_progress = 0;
    }
    fluid_palette_color_t base = blend_pastel_colors(s_fluid_morph_progress);
    for (int tint = 0; tint < FLUID_TINT_COUNT; ++tint) {
        uint8_t lighten = (uint16_t)(FLUID_TINT_COUNT - 1 - tint) *
                          FLUID_TINT_LIGHTEN_MAX / (FLUID_TINT_COUNT - 1);
        s_fluid_tints[tint] = (fluid_palette_color_t){
            .red = base.red + ((uint16_t)(255 - base.red) * lighten) / 255,
            .green = base.green + ((uint16_t)(255 - base.green) * lighten) / 255,
            .blue = base.blue + ((uint16_t)(255 - base.blue) * lighten) / 255,
        };
    }
    for (int hue = 0; hue < FLUID_TABLE_SIZE; ++hue) {
        s_fluid_palette[hue] = fluid_palette_color(hue);
    }
    ++s_fluid_morph_progress;
}

static uint16_t fluid_color(int x, int y, uint8_t phase)
{
    uint8_t horizontal = s_fluid_sine[(uint8_t)(x * 2 + phase * 2)];
    uint8_t vertical = s_fluid_sine[(uint8_t)(y * 2 - phase)];
    uint8_t diagonal = s_fluid_sine[(uint8_t)(x + y + phase * 3)];
    uint8_t hue = s_fluid_average[horizontal + vertical + diagonal] + phase;
    return s_fluid_palette[hue];
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
            for (int scaled_x = 0; scaled_x < FLUID_SCALE; ++scaled_x) {
                destination[destination_x + scaled_x] = color;
            }
        }
        for (int scaled_y = 1; scaled_y < FLUID_SCALE; ++scaled_y) {
            memcpy(destination + scaled_y * TFT_WIDTH, destination,
                   TFT_WIDTH * sizeof(*destination));
        }
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
        int pixel_count = TFT_WIDTH * rows;
        const uint16_t *source = &s_framebuffer[row * TFT_WIDTH];
        for (int index = 0; index < pixel_count; ++index) {
            s_transfer_buffer[index] = __builtin_bswap16(source[index]);
        }
        spi_transaction_t transaction = {
            .length = (size_t)pixel_count * 16,
            .tx_buffer = s_transfer_buffer,
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
    if ((s_fluid_palette_tick++ & (FLUID_PALETTE_UPDATE_INTERVAL - 1)) == 0) {
        update_fluid_palette();
    }
    for (int y = 0; y < FLUID_FIELD_HEIGHT; ++y) {
        for (int x = 0; x < FLUID_FIELD_WIDTH; ++x) {
            s_fluid_field[y * FLUID_FIELD_WIDTH + x] =
                fluid_color(x * FLUID_SCALE, y * FLUID_SCALE, phase);
        }
    }
    apply_gaussian_blur();
    upscale_fluid_field();
    draw_fps_counter();
    esp_err_t err = flush_framebuffer();
    if (err == ESP_OK) {
        update_fps_counter();
    }
    return err;
}
