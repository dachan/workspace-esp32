#include "serial_model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "clock.h"
#include "front_title.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_sync.h"

static const char *TAG = "serial_model";

static char s_line[SERIAL_LINE_MAX];
static size_t s_len;
static bool s_discard_line;

esp_err_t serial_model_init(void)
{
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 1024,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    s_len = 0;
    s_discard_line = false;
    ESP_LOGI(TAG, "USB Serial/JTAG ready for SYNC/STATE/ACK and legacy display updates");
    return ESP_OK;
}

static int handle_line(const char *line, model_fields_t *fields)
{
    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    if (*line == '\0') {
        return 0;
    }

    if (serial_sync_handle_line(line)) {
        return 0;
    }
    if (clock_apply_line(line)) {
        return 0;
    }
    if (front_title_apply_line(line)) {
        return 0;
    }

    /* Outbound SET lines must not be treated as display updates. */
    if (strncmp(line, "SET ", 4) == 0 || strncmp(line, "SET\t", 4) == 0) {
        ESP_LOGD(TAG, "ignore SET line");
        return 0;
    }

    if (strncmp(line, "MODEL ", 6) == 0 || strncmp(line, "MODEL\t", 6) == 0) {
        model_fields_t parsed;
        model_parse_name(line + 6, &parsed);
        int model = catalog_model_index(parsed.model);
        if (model < 0) {
            ESP_LOGW(TAG, "ignore unknown MODEL");
            return 0;
        }
        snprintf(parsed.model, sizeof(parsed.model), "%s", catalog_model_at(model));
        if (parsed.has_thinking) {
            int level = catalog_thinking_level(parsed.model, parsed.thinking);
            if (level > 0) {
                snprintf(parsed.thinking, sizeof(parsed.thinking), "%s", catalog_thinking_name(parsed.model, level));
            }
        }
        *fields = parsed;
        return 1;
    }

    if (strncmp(line, "THINKING ", 9) == 0 || strncmp(line, "THINKING\t", 9) == 0) {
        const char *value = line + 9;
        while (*value && isspace((unsigned char)*value)) value++;
        char name[MODEL_PARSE_MAX];
        size_t n = strlen(value);
        while (n && isspace((unsigned char)value[n - 1])) n--;
        if (n >= sizeof(name)) return 0;
        memcpy(name, value, n);
        name[n] = '\0';
        int level = catalog_thinking_level(fields->model, name);
        if (!level) {
            ESP_LOGW(TAG, "ignore unknown THINKING");
            return 0;
        }
        snprintf(fields->thinking, sizeof(fields->thinking), "%s", catalog_thinking_name(fields->model, level));
        fields->has_thinking = 1;
        return 1;
    }

    ESP_LOGD(TAG, "ignore line: %s", line);
    return 0;
}

int serial_model_poll(model_fields_t *fields)
{
    uint8_t chunk[64];
    int updated = 0;

    // Leave time for encoder polling even under continuous serial traffic.
    for (int reads = 0; reads < 4; reads++) {
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;
        }
        for (int i = 0; i < n; i++) {
            char c = (char)chunk[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                s_line[s_len] = '\0';
                if (!s_discard_line && handle_line(s_line, fields)) {
                    updated = 1;
                }
                s_len = 0;
                s_discard_line = false;
                continue;
            }
            if (s_discard_line) {
                continue;
            }
            if (c == '\0') {
                s_discard_line = true;
                continue;
            }
            if (s_len + 1 < sizeof(s_line)) {
                s_line[s_len++] = c;
            } else {
                s_discard_line = true;
                ESP_LOGW(TAG, "discard oversized serial line");
            }
        }
    }
    return updated;
}

bool serial_model_write_line(const char *line)
{
    if (!line || !line[0]) {
        return false;
    }
    char frame[SERIAL_LINE_MAX + 2];
    // Leading newline restores framing after a disconnect mid-transfer.
    int n = snprintf(frame, sizeof(frame), "\n%s\n", line);
    if (n <= 0 || n >= (int)sizeof(frame)) {
        return false;
    }
    // IDF enqueues this ring-buffer item in full or returns zero. Its ISR owns
    // FIFO flushing. Never block input sampling while a host is disconnected.
    return usb_serial_jtag_write_bytes(frame, n, 0) == n;
}
