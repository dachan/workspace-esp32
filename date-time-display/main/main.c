#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"
#include "st7735.h"

static const char *TAG = "date-time";

static int month_number(const char *month)
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; ++i) {
        if (strncmp(month, names[i], 3) == 0) {
            return i;
        }
    }
    return 0;
}

static time_t build_time(void)
{
    char month[4] = {0};
    int day = 1;
    int year = 2026;
    int hour = 0;
    int minute = 0;
    int second = 0;
    sscanf(__DATE__, "%3s %d %d", month, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
    struct tm value = {
        .tm_sec = second,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day,
        .tm_mon = month_number(month),
        .tm_year = year - 1900,
        .tm_isdst = -1,
    };
    return mktime(&value);
}

static void persist_time(time_t value)
{
    nvs_handle_t handle;
    if (nvs_open("clock", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_i64(handle, "epoch", (int64_t)value);
    nvs_commit(handle);
    nvs_close(handle);
}

static time_t load_time(void)
{
    nvs_handle_t handle;
    int64_t epoch = 0;
    if (nvs_open("clock", NVS_READONLY, &handle) == ESP_OK) {
        esp_err_t err = nvs_get_i64(handle, "epoch", &epoch);
        nvs_close(handle);
        if (err == ESP_OK && epoch > 0) {
            return (time_t)epoch;
        }
    }
    return build_time();
}

static bool s_usb_serial_ready;

static void serial_write(const char *text)
{
    if (s_usb_serial_ready) {
        usb_serial_jtag_write_bytes(text, strlen(text), 0);
    } else {
        fputs(text, stdout);
        fflush(stdout);
    }
}

static void print_current_time(void)
{
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char date[16];
    char clock_text[16];
    strftime(date, sizeof(date), "%Y-%m-%d", &local);
    strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &local);
    char line[48];
    snprintf(line, sizeof(line), "TIME %s %s\n", date, clock_text);
    serial_write(line);
}

static void serial_task(void *arg)
{
    (void)arg;
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 1024,
    };
    esp_err_t driver_err = usb_serial_jtag_driver_install(&config);
    if (driver_err != ESP_OK) {
        ESP_LOGW(TAG, "USB Serial/JTAG input unavailable: %s", esp_err_to_name(driver_err));
        vTaskDelete(NULL);
        return;
    }
    s_usb_serial_ready = true;
    ESP_LOGI(TAG, "USB Serial/JTAG clock input ready");

    char line[64];
    size_t line_length = 0;
    uint8_t chunk[64];
    while (true) {
        int count = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), pdMS_TO_TICKS(20));
        if (count <= 0) {
            continue;
        }
        for (int index = 0; index < count; ++index) {
            char c = (char)chunk[index];
            if (c == '\r') {
                continue;
            }
            if (c != '\n') {
                if (line_length + 1 < sizeof(line)) {
                    line[line_length++] = c;
                }
                continue;
            }
            line[line_length] = '\0';
            line_length = 0;

        int year, month, day, hour, minute, second;
        if (sscanf(line, "TIME %d-%d-%d %d:%d:%d",
                   &year, &month, &day, &hour, &minute, &second) == 6) {
            struct tm value = {
                .tm_sec = second,
                .tm_min = minute,
                .tm_hour = hour,
                .tm_mday = day,
                .tm_mon = month - 1,
                .tm_year = year - 1900,
                .tm_isdst = -1,
            };
            time_t epoch = mktime(&value);
            struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
            settimeofday(&tv, NULL);
            persist_time(epoch);
            char response[48];
            snprintf(response, sizeof(response), "TIME SET %04d-%02d-%02d %02d:%02d:%02d\n",
                     year, month, day, hour, minute, second);
            serial_write(response);
        } else if (strncmp(line, "TIME", 4) == 0) {
            print_current_time();
        } else {
            serial_write("Commands: TIME YYYY-MM-DD HH:MM:SS | TIME\n");
        }
        }
    }
}

void app_main(void)
{
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK && nvs_err != ESP_ERR_NVS_NO_FREE_PAGES &&
        nvs_err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init: %s; clock persistence disabled", esp_err_to_name(nvs_err));
    }
    time_t initial = load_time();
    struct timeval tv = {.tv_sec = initial, .tv_usec = 0};
    settimeofday(&tv, NULL);

    ESP_ERROR_CHECK(st7735_init());
    bool oled_ready = ssd1306_init() == ESP_OK;
    if (!oled_ready) {
        ESP_LOGW(TAG, "OLED is offline; TFT will continue and OLED will retry after reset");
    }
    xTaskCreate(serial_task, "serial", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "dual display date/time ready; send TIME YYYY-MM-DD HH:MM:SS to set clock");

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        char date[16];
        char clock_text[16];
        strftime(date, sizeof(date), "%Y-%m-%d", &local);
        strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &local);
        esp_err_t tft_err = st7735_render(date, clock_text);
        esp_err_t oled_err = oled_ready ? ssd1306_render(date, clock_text) : ESP_ERR_INVALID_STATE;
        if (tft_err != ESP_OK || oled_err != ESP_OK) {
            ESP_LOGW(TAG, "render failed: TFT=%s OLED=%s",
                     esp_err_to_name(tft_err), esp_err_to_name(oled_err));
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
