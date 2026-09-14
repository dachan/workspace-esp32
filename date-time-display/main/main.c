#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

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

static void print_current_time(void)
{
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char date[16];
    char clock_text[16];
    strftime(date, sizeof(date), "%Y-%m-%d", &local);
    strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &local);
    printf("TIME %s %s\n", date, clock_text);
    fflush(stdout);
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[64];
    while (fgets(line, sizeof(line), stdin) != NULL) {
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
            printf("TIME SET %04d-%02d-%02d %02d:%02d:%02d\n",
                   year, month, day, hour, minute, second);
            fflush(stdout);
        } else if (strncmp(line, "TIME", 4) == 0) {
            print_current_time();
        } else {
            printf("Commands: TIME YYYY-MM-DD HH:MM:SS | TIME\n");
            fflush(stdout);
        }
    }
    vTaskDelete(NULL);
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
    ESP_ERROR_CHECK(ssd1306_init());
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
        esp_err_t oled_err = ssd1306_render(date, clock_text);
        if (tft_err != ESP_OK || oled_err != ESP_OK) {
            ESP_LOGW(TAG, "render failed: TFT=%s OLED=%s",
                     esp_err_to_name(tft_err), esp_err_to_name(oled_err));
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
