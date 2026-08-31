#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "board_rgb_led.h"
#include "display_sync.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_psram.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "joystick.h"
#include "ld2450.h"
#include "microphone.h"
#include "radar_link.h"
#include "sdmmc_cmd.h"
#include "screensaver.h"
#include "speech_recognition.h"
#include "touch_calibration.h"
#include "display_profile.h"
#include "rainbow_wave.h"

static const char *TAG = "hardware_test";

enum {
    PIN_SD_CS = 4,
    PIN_TOUCH_INT = 5,
    PIN_TOUCH_SDA = 6,
    PIN_TOUCH_RST = 7,
    PIN_LCD_MOSI = 8,
    PIN_LCD_DC = 9,
    PIN_LCD_RST = 10,
    PIN_LCD_CS = 11,
    PIN_TOUCH_SCL = 15,
    PIN_LCD_MISO = 16,
    PIN_BACKLIGHT = 17,
    PIN_LCD_SCLK = 18,
};

enum {
    LCD_TRANSFER_ROWS = 80,
    TOUCH_ADDR = 0x38,
};

#define LCD_HOST SPI2_HOST
#define TOUCH_PORT I2C_NUM_0

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static bool s_touch_found;
static joystick_t s_joystick;
static bool s_joystick_ready;
static bool s_microphone_ready;
static bool s_speech_ready;
static bool s_radar_ready;
static bool s_radar_has_frame;
static ld2450_frame_t s_radar_frame;
static radar_link_frame_t s_link_frame;
static TickType_t s_radar_last_frame;
static TickType_t s_radar_last_report;
static TickType_t s_radar_last_ping;

static uint16_t rgb565_wire(uint8_t red, uint8_t green, uint8_t blue)
{
    uint16_t value = ((uint16_t)(red & 0xf8) << 8)
                   | ((uint16_t)(green & 0xfc) << 3)
                   | ((uint16_t)blue >> 3);
    return __builtin_bswap16(value);
}

static void log_result(const char *name, bool passed, const char *detail)
{
    ESP_LOGI(TAG, "TEST %-18s %s%s%s", name, passed ? "PASS" : "FAIL",
             detail ? " - " : "", detail ? detail : "");
}

static void test_core_and_memory(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_err_t flash_err = esp_flash_get_size(NULL, &flash_size);
    char detail[128];
    snprintf(detail, sizeof(detail), "%d cores, rev %d.%d, Wi-Fi=%s, BLE=%s",
             chip.cores, chip.revision / 100, chip.revision % 100,
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no",
             (chip.features & CHIP_FEATURE_BLE) ? "yes" : "no");
    log_result("ESP32-S3 core", chip.model == CHIP_ESP32S3 && chip.cores == 2, detail);

    snprintf(detail, sizeof(detail), "%" PRIu32 " bytes", flash_size);
    log_result("flash", flash_err == ESP_OK && flash_size == 16 * 1024 * 1024, detail);

    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t test_bytes = psram_total > 4 * 1024 * 1024 ? 4 * 1024 * 1024 : psram_total / 2;
    uint32_t *buffer = heap_caps_malloc(test_bytes, MALLOC_CAP_SPIRAM);
    bool psram_ok = esp_psram_is_initialized() && buffer != NULL && test_bytes > 0;
    if (psram_ok) {
        size_t words = test_bytes / sizeof(*buffer);
        for (size_t i = 0; i < words; ++i) {
            buffer[i] = 0xa5a50000u ^ (uint32_t)i;
        }
        for (size_t i = 0; i < words; ++i) {
            if (buffer[i] != (0xa5a50000u ^ (uint32_t)i)) {
                psram_ok = false;
                break;
            }
        }
    }
    free(buffer);
    snprintf(detail, sizeof(detail), "%zu bytes detected; %zu bytes pattern-tested",
             psram_total, test_bytes);
    log_result("PSRAM", psram_ok && psram_total >= 8 * 1024 * 1024, detail);
}

static esp_err_t i2c_probe(uint8_t address)
{
    i2c_cmd_handle_t command = i2c_cmd_link_create();
    if (!command) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(command);
    i2c_master_write_byte(command, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(command);
    esp_err_t result = i2c_master_cmd_begin(TOUCH_PORT, command, pdMS_TO_TICKS(30));
    i2c_cmd_link_delete(command);
    return result;
}

static esp_err_t ft6336_read(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_write_read_device(TOUCH_PORT, TOUCH_ADDR, &reg, 1, data,
                                        length, pdMS_TO_TICKS(100));
}

static bool touch_read_point(uint16_t *raw_x, uint16_t *raw_y)
{
    uint8_t data[5] = {0};
    if (ft6336_read(0x02, data, sizeof(data)) != ESP_OK || (data[0] & 0x0f) == 0) {
        return false;
    }
    *raw_x = ((uint16_t)(data[1] & 0x0f) << 8) | data[2];
    *raw_y = ((uint16_t)(data[3] & 0x0f) << 8) | data[4];
    return true;
}

static void init_and_test_touch(void)
{
    gpio_config_t reset = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&reset));
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(250));

    gpio_config_t interrupt = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&interrupt));

    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_PORT, &config));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_PORT, config.mode, 0, 0, 0));

    char addresses[128] = {0};
    size_t used = 0;
    for (uint8_t address = 1; address < 0x7f; ++address) {
        if (i2c_probe(address) == ESP_OK) {
            used += snprintf(addresses + used, sizeof(addresses) - used,
                             "%s0x%02x", used ? ", " : "", address);
            if (used >= sizeof(addresses)) {
                break;
            }
        }
    }
    s_touch_found = i2c_probe(TOUCH_ADDR) == ESP_OK;
    log_result("touch I2C", s_touch_found, used ? addresses : "no devices found");

    if (s_touch_found) {
        uint8_t chip_id = 0xff;
        uint8_t vendor_id = 0xff;
        esp_err_t chip_err = ft6336_read(0xa3, &chip_id, 1);
        esp_err_t vendor_err = ft6336_read(0xa8, &vendor_id, 1);
        char detail[96];
        snprintf(detail, sizeof(detail), "address 0x38, chip=0x%02x, vendor=0x%02x, INT=%d",
                 chip_id, vendor_id, gpio_get_level(PIN_TOUCH_INT));
        log_result("FT6336U", chip_err == ESP_OK && vendor_err == ESP_OK, detail);
    }
}
static void draw_solid_rect(int x, int y, int width, int height, uint16_t color)
{
    const int rows_per_chunk = 16;
    uint16_t *pixels = heap_caps_malloc(width * rows_per_chunk * sizeof(*pixels), MALLOC_CAP_DMA);
    if (!pixels) {
        ESP_LOGE(TAG, "display draw buffer allocation failed");
        return;
    }
    for (int i = 0; i < width * rows_per_chunk; ++i) {
        pixels[i] = color;
    }
    for (int row = 0; row < height; row += rows_per_chunk) {
        int rows = height - row < rows_per_chunk ? height - row : rows_per_chunk;
        ESP_ERROR_CHECK(display_sync_draw(s_panel, x, y + row, x + width, y + row + rows,
                                          pixels));
    }
    free(pixels);
}

static void draw_display_pattern(void)
{
    const uint16_t colors[] = {
        rgb565_wire(255, 0, 0),
        rgb565_wire(0, 255, 0),
        rgb565_wire(0, 0, 255),
        rgb565_wire(255, 255, 255),
        rgb565_wire(0, 0, 0),
        rgb565_wire(255, 255, 0),
    };
    const int band_height = LCD_HEIGHT / 6;
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        int y = (int)i * band_height;
        int height = i == 5 ? LCD_HEIGHT - y : band_height;
        draw_solid_rect(0, y, LCD_WIDTH, height, colors[i]);
    }
}

static void init_and_test_display(bool show_pattern)
{
    gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight));
    gpio_set_level(PIN_BACKLIGHT, 0);
    if (show_pattern) {
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(PIN_BACKLIGHT, 1);
        log_result("backlight GPIO17", true, "pulsed off, then on");
    }

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_TRANSFER_ROWS * sizeof(uint16_t) + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                              &io_config, &s_panel_io));
    ESP_ERROR_CHECK(display_sync_init(s_panel_io));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(display_profile_create_panel(s_panel_io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    uint8_t display_id[4] = {0};
    esp_err_t id_err = esp_lcd_panel_io_rx_param(s_panel_io, 0x04, display_id, sizeof(display_id));
    char detail[96];
    snprintf(detail, sizeof(detail), "RDDID=%02x %02x %02x %02x (%s)",
             display_id[0], display_id[1], display_id[2], display_id[3], esp_err_to_name(id_err));
    bool readback = id_err == ESP_OK
                 && memcmp(display_id, "\0\0\0\0", sizeof(display_id)) != 0
                 && memcmp(display_id, "\xff\xff\xff\xff", sizeof(display_id)) != 0;
    log_result("display MISO", readback, detail);

    if (show_pattern) {
        draw_display_pattern();
        char detail[96];
        snprintf(detail, sizeof(detail), "six colour bands sent to %s", DISPLAY_PROFILE_NAME);
        log_result("display draw", true, detail);
    }
}

static void test_sd_card(void)
{
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = LCD_HOST;
    device.gpio_cs = PIN_SD_CS;

    sdspi_dev_handle_t handle;
    esp_err_t result = sdspi_host_init_device(&device, &handle);
    if (result != ESP_OK) {
        log_result("microSD", false, esp_err_to_name(result));
        return;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = handle;
    host.max_freq_khz = SDMMC_FREQ_PROBING;
    sdmmc_card_t card = {0};
    result = sdmmc_card_init(&host, &card);
    if (result == ESP_OK) {
        char detail[128];
        uint64_t capacity = (uint64_t)card.csd.capacity * card.csd.sector_size;
        snprintf(detail, sizeof(detail), "%s, %.2f GiB, sector=%" PRIu32,
                 card.cid.name, (double)capacity / (1024.0 * 1024.0 * 1024.0),
                 (uint32_t)card.csd.sector_size);
        log_result("microSD", true, detail);
    } else {
        char detail[96];
        snprintf(detail, sizeof(detail), "%s (insert/reseat a card if none is present)",
                 esp_err_to_name(result));
        log_result("microSD", false, detail);
    }
    sdspi_host_remove_device(handle);
}

static void update_joystick(screensaver_t *screensaver)
{
    voice_command_t command = s_speech_ready ? speech_recognition_take_command() : VOICE_COMMAND_NONE;
    int target_x = screensaver->cursor_x;
    int target_y = screensaver->cursor_y;

    switch (command) {
    case VOICE_COMMAND_UP:
        // The 2.8-inch panel is physically mounted a quarter-turn clockwise
        // from its controller coordinate system.
        target_x = 8;
        target_y = LCD_HEIGHT / 2;
        break;
    case VOICE_COMMAND_DOWN:
        target_x = LCD_WIDTH - 9;
        target_y = LCD_HEIGHT / 2;
        break;
    case VOICE_COMMAND_LEFT:
        target_x = LCD_WIDTH / 2;
        target_y = 8;
        break;
    case VOICE_COMMAND_RIGHT:
        target_x = LCD_WIDTH / 2;
        target_y = LCD_HEIGHT - 9;
        break;
    case VOICE_COMMAND_UP_RIGHT:
        target_x = 8;
        target_y = LCD_HEIGHT - 9;
        break;
    case VOICE_COMMAND_UP_LEFT:
        target_x = 8;
        target_y = 8;
        break;
    case VOICE_COMMAND_DOWN_RIGHT:
        target_x = LCD_WIDTH - 9;
        target_y = LCD_HEIGHT - 9;
        break;
    case VOICE_COMMAND_DOWN_LEFT:
        target_x = LCD_WIDTH - 9;
        target_y = 8;
        break;
    case VOICE_COMMAND_RESET:
        target_x = LCD_WIDTH / 2;
        target_y = LCD_HEIGHT / 2;
        break;
    case VOICE_COMMAND_NONE:
        break;
    }

    if (command != VOICE_COMMAND_NONE) {
        screensaver_set_cursor(screensaver, target_x, target_y);
        ESP_LOGI(TAG, "voice command %d -> cursor (%d, %d)", command, target_x, target_y);
    } else if (s_speech_ready && speech_recognition_take_sound_detected()) {
        screensaver_set_cursor(screensaver, LCD_WIDTH / 2, LCD_HEIGHT / 2);
    } else if (!s_speech_ready && s_microphone_ready && microphone_sound_detected()) {
        screensaver_set_cursor(screensaver, LCD_WIDTH / 2, LCD_HEIGHT / 2);
    }
    if (!s_joystick_ready) {
        return;
    }

    joystick_input_t input;
    if (joystick_read(&s_joystick, &input) != ESP_OK) {
        return;
    }
    screensaver_set_cursor(screensaver, screensaver->cursor_x + input.cursor_dx,
                           screensaver->cursor_y + input.cursor_dy);
    if (input.switch_pressed) {
        rainbow_wave_next_palette();
        ESP_LOGI(TAG, "JOYSTICK PRESS: next palette");
    }
}

static void update_radar(screensaver_t *screensaver, TickType_t now)
{
#if defined(RADAR_LINK_ROLE_RECEIVER)
    bool received = radar_link_receive(&s_link_frame);
    if (received) {
        for (int index = 0; index < LD2450_MAX_TARGETS; ++index) {
            s_radar_frame.targets[index] = (ld2450_target_t) {
                .x_mm = s_link_frame.targets[index].x_mm,
                .y_mm = s_link_frame.targets[index].y_mm,
                .active = s_link_frame.targets[index].active,
            };
        }
    }
#else
    bool received = s_radar_ready && ld2450_poll(&s_radar_frame);
#if defined(RADAR_LINK_ROLE_TRANSMITTER)
    if (received) {
        for (int index = 0; index < LD2450_MAX_TARGETS; ++index) {
            s_link_frame.targets[index] = (radar_link_target_t) {
                .x_mm = s_radar_frame.targets[index].x_mm,
                .y_mm = s_radar_frame.targets[index].y_mm,
                .active = s_radar_frame.targets[index].active,
            };
        }
        esp_err_t send_error = radar_link_send(&s_link_frame);
        if (send_error != ESP_OK && send_error != ESP_ERR_ESPNOW_NO_MEM) {
            ESP_LOGW(TAG, "radar broadcast failed: %s", esp_err_to_name(send_error));
        }
    }
#endif
#endif
    if (received) {
        s_radar_has_frame = true;
        s_radar_last_frame = now;
    }

    bool connected = s_radar_has_frame
                  && now - s_radar_last_frame < pdMS_TO_TICKS(1500);
    screensaver_person_t people[SCREENSAVER_MAX_PEOPLE] = {0};
    if (connected) {
        for (int index = 0; index < SCREENSAVER_MAX_PEOPLE; ++index) {
            people[index] = (screensaver_person_t) {
                .x_mm = s_radar_frame.targets[index].x_mm,
                .y_mm = s_radar_frame.targets[index].y_mm,
                .active = s_radar_frame.targets[index].active,
            };
        }
    }
    screensaver_set_people(screensaver, people, connected);

    bool has_target = false;
    for (int index = 0; index < SCREENSAVER_MAX_PEOPLE; ++index) {
        has_target = has_target || people[index].active;
    }
    if (!connected || !has_target) {
        s_radar_last_ping = 0;
    } else if (s_radar_last_ping == 0
               || now - s_radar_last_ping >= pdMS_TO_TICKS(2000)) {
        screensaver_trigger_radar_ping(screensaver);
        s_radar_last_ping = now;
    }

    if (received && now - s_radar_last_report >= pdMS_TO_TICKS(1000)) {
        int target_count = 0;
        for (int index = 0; index < LD2450_MAX_TARGETS; ++index) {
            target_count += s_radar_frame.targets[index].active ? 1 : 0;
        }
        ESP_LOGI(TAG,
                 "LD2450 targets=%d t1=(%d,%d) t2=(%d,%d) t3=(%d,%d) mm",
                 target_count,
                 s_radar_frame.targets[0].x_mm, s_radar_frame.targets[0].y_mm,
                 s_radar_frame.targets[1].x_mm, s_radar_frame.targets[1].y_mm,
                 s_radar_frame.targets[2].x_mm, s_radar_frame.targets[2].y_mm);
        s_radar_last_report = now;
    }
}

static bool monitor_touch(const touch_calibration_t *calibration)
{
    uint32_t touch_count = 0;
    bool was_touched = false;
    TickType_t touch_started_at = 0;
    TickType_t last_report = xTaskGetTickCount();
    TickType_t last_screensaver_step = last_report - pdMS_TO_TICKS(33);
    screensaver_t screensaver;
    screensaver_start(s_panel, &screensaver);

    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (s_touch_found) {
            uint16_t x;
            uint16_t y;
            bool touched = touch_read_point(&x, &y);
            if (touched) {
                uint16_t screen_x = x;
                uint16_t screen_y = y;
                if (touched && calibration) {
                    touch_calibration_apply(calibration, x, y, &screen_x, &screen_y);
                }
                // Touch coordinates are calibrated to the display, so a tap
                // or drag places the reticle exactly under the finger.
                screensaver_set_cursor(&screensaver, screen_x, screen_y);
                if (touched && !was_touched) {
                    touch_started_at = now;
                    ++touch_count;
                    rainbow_wave_trigger();
                    ESP_LOGI(TAG, "TOUCH #%" PRIu32 " raw=(%u,%u) screen=(%u,%u) INT=%d",
                             touch_count, x, y, screen_x, screen_y, gpio_get_level(PIN_TOUCH_INT));
                }
                if (touched && now - touch_started_at >= pdMS_TO_TICKS(3000)) {
                    ESP_LOGI(TAG, "RECALIBRATION REQUESTED: three-second touch hold");
                    return true;
                }
                was_touched = touched;
            } else {
                was_touched = false;
            }
        }

        if (now - last_screensaver_step >= pdMS_TO_TICKS(33)) {
            // Apply joystick motion immediately before rendering so a fast
            // movement cannot accumulate multiple unseen position changes.
            update_joystick(&screensaver);
            update_radar(&screensaver, now);
            rainbow_wave_step();
            screensaver_set_palette(&screensaver, rainbow_wave_palette_index(),
                                    rainbow_wave_color_phase());
            screensaver_step(s_panel, &screensaver);
            last_screensaver_step = now;
        }
        if (now - last_report >= pdMS_TO_TICKS(5000)) {
            ESP_LOGI(TAG, "HEARTBEAT touches=%" PRIu32 " touch_INT=%d",
                     touch_count, gpio_get_level(PIN_TOUCH_INT));
            last_report = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void power_off(void)
{
    ESP_LOGW(TAG, "POWERING OFF: display, backlight, and LEDs off; entering deep sleep");
    rainbow_wave_off();
    board_rgb_led_off();
    gpio_set_level(PIN_BACKLIGHT, 0);
    if (s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, false);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 DISPLAY/TOUCH/SD HARDWARE TEST START");
    ESP_LOGI(TAG, "GPIO map: SD_CS=4 TOUCH_INT=5 SDA=6 RST=7 SCL=15 MISO=16 BL=17 SCK=18 MOSI=8 DC=9 LCD_RST=10 LCD_CS=11 JOY_X=12 JOY_Y=13 JOY_SW=14");

    board_rgb_led_off();
    esp_err_t rainbow_err = rainbow_wave_init();
    log_result("rainbow GPIO1", rainbow_err == ESP_OK,
               rainbow_err == ESP_OK ? "external 8-pixel bar" : esp_err_to_name(rainbow_err));
    esp_err_t joystick_err = joystick_init(&s_joystick);
    s_joystick_ready = joystick_err == ESP_OK;
    log_result("joystick 12/13/14", s_joystick_ready,
               s_joystick_ready ? "cursor ready; press for next palette"
                                : esp_err_to_name(joystick_err));
    esp_err_t microphone_err = microphone_init();
    s_microphone_ready = microphone_err == ESP_OK;
    log_result("microphone 38/39/40", s_microphone_ready,
               s_microphone_ready ? "sound resets reticle" : esp_err_to_name(microphone_err));
    esp_err_t speech_err = s_microphone_ready ? speech_recognition_init() : ESP_ERR_INVALID_STATE;
    s_speech_ready = speech_err == ESP_OK;
    log_result("speech directions", s_speech_ready,
               s_speech_ready ? "eight directions plus reset" : esp_err_to_name(speech_err));
#if defined(RADAR_LINK_ROLE_RECEIVER)
    esp_err_t radar_err = radar_link_init();
    s_radar_ready = radar_err == ESP_OK;
    log_result("remote radar", s_radar_ready,
               s_radar_ready ? radar_link_role_name() : esp_err_to_name(radar_err));
#else
    esp_err_t radar_err = ld2450_init();
    s_radar_ready = radar_err == ESP_OK;
    log_result("LD2450 41/42", s_radar_ready,
               s_radar_ready ? "UART ready; waiting for 30-byte target frames"
                             : esp_err_to_name(radar_err));
    if (s_radar_ready) {
        esp_err_t tracking_err = ld2450_set_single_target_mode();
        log_result("LD2450 tracking", tracking_err == ESP_OK,
                   tracking_err == ESP_OK ? "single-target mode confirmed"
                                          : esp_err_to_name(tracking_err));
        ld2450_firmware_version_t firmware_version;
        esp_err_t version_err = ld2450_get_firmware_version(&firmware_version);
        char version_detail[48];
        if (version_err == ESP_OK) {
            snprintf(version_detail, sizeof(version_detail), "V%u.%02u.%08" PRIx32,
                     firmware_version.major, firmware_version.minor,
                     firmware_version.build);
        }
        log_result("LD2450 firmware", version_err == ESP_OK,
                   version_err == ESP_OK ? version_detail : esp_err_to_name(version_err));
    }
#if defined(RADAR_LINK_ROLE_TRANSMITTER)
    esp_err_t link_err = radar_link_init();
    log_result("radar broadcast", link_err == ESP_OK,
               link_err == ESP_OK ? radar_link_role_name() : esp_err_to_name(link_err));
#endif
#endif
    touch_calibration_t calibration = {0};
    bool has_saved_calibration = touch_calibration_load(&calibration);
    test_core_and_memory();
    init_and_test_touch();
    init_and_test_display(!has_saved_calibration);
    test_sd_card();

    if (!s_touch_found) {
        gpio_set_level(PIN_BACKLIGHT, 1);
        screensaver_t screensaver;
        screensaver_start(s_panel, &screensaver);
        ESP_LOGW(TAG, "DISPLAY-ONLY MODE: starting screensaver without touch input");
        TickType_t last_screensaver_step = xTaskGetTickCount() - pdMS_TO_TICKS(33);
        while (true) {
            TickType_t now = xTaskGetTickCount();
            if (now - last_screensaver_step >= pdMS_TO_TICKS(33)) {
                update_joystick(&screensaver);
                update_radar(&screensaver, now);
                rainbow_wave_step();
                screensaver_set_palette(&screensaver, rainbow_wave_palette_index(),
                                        rainbow_wave_color_phase());
                screensaver_step(s_panel, &screensaver);
                last_screensaver_step = now;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    const touch_calibration_t *active_calibration = has_saved_calibration ? &calibration : NULL;
    if (!has_saved_calibration) {
        ESP_LOGI(TAG, "NO SAVED CALIBRATION - starting screensaver; hold any touch for three seconds to calibrate");
    }
    while (s_touch_found) {
        gpio_set_level(PIN_BACKLIGHT, 1);
        ESP_LOGI(TAG, "STARTING SCREENSAVER");
        if (!monitor_touch(active_calibration)) {
            break;
        }
        ESP_LOGI(TAG, "Starting touch calibration; lift your finger, then tap each target");
        if (touch_calibration_run(s_panel, touch_read_point, power_off,
                                  PIN_BACKLIGHT, &calibration)) {
            active_calibration = &calibration;
        } else {
            active_calibration = NULL;
            ESP_LOGW(TAG, "CALIBRATION DID NOT COMPLETE - returning to the screensaver");
        }
    }
    ESP_LOGE(TAG, "TOUCH MONITOR STOPPED - restarting in 5 seconds");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
