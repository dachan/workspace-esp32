#include "serial_model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"

static const char *TAG = "serial_model";

#define SERIAL_LINE_MAX 192

static char s_line[SERIAL_LINE_MAX];
static size_t s_len;
static char s_thinking_override[MODEL_PARSE_MAX];
static int s_has_override;

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
    s_has_override = 0;
    s_thinking_override[0] = '\0';
    ESP_LOGI(TAG, "USB Serial/JTAG ready for MODEL/THINKING RX and SET TX @ 115200");
    return ESP_OK;
}

static void apply_override(model_fields_t *fields)
{
    if (s_has_override) {
        strncpy(fields->thinking, s_thinking_override, sizeof(fields->thinking) - 1);
        fields->thinking[sizeof(fields->thinking) - 1] = '\0';
        fields->has_thinking = fields->thinking[0] != '\0';
    }
}

static int handle_line(const char *line, model_fields_t *fields)
{
    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    if (*line == '\0') {
        return 0;
    }

    /* Outbound SET lines must not be treated as display updates. */
    if (strncmp(line, "SET ", 4) == 0 || strncmp(line, "SET\t", 4) == 0) {
        ESP_LOGD(TAG, "ignore SET line");
        return 0;
    }

    if (strncmp(line, "MODEL ", 6) == 0 || strncmp(line, "MODEL\t", 6) == 0) {
        s_has_override = 0;
        s_thinking_override[0] = '\0';
        model_parse_name(line + 6, fields);
        ESP_LOGI(TAG, "MODEL raw='%s' -> model='%s' thinking='%s'",
                 line + 6, fields->model, fields->has_thinking ? fields->thinking : "-");
        return fields->has_model;
    }

    if (strncmp(line, "THINKING ", 9) == 0 || strncmp(line, "THINKING\t", 9) == 0) {
        const char *val = line + 9;
        while (*val && isspace((unsigned char)*val)) {
            val++;
        }
        strncpy(s_thinking_override, val, sizeof(s_thinking_override) - 1);
        s_thinking_override[sizeof(s_thinking_override) - 1] = '\0';
        // trim trailing spaces
        size_t n = strlen(s_thinking_override);
        while (n > 0 && isspace((unsigned char)s_thinking_override[n - 1])) {
            s_thinking_override[--n] = '\0';
        }
        s_has_override = s_thinking_override[0] != '\0';
        if (fields->has_model) {
            apply_override(fields);
        } else {
            memset(fields, 0, sizeof(*fields));
            apply_override(fields);
        }
        ESP_LOGI(TAG, "THINKING override='%s'", s_thinking_override);
        return 1;
    }

    ESP_LOGD(TAG, "ignore line: %s", line);
    return 0;
}

int serial_model_poll(model_fields_t *fields)
{
    uint8_t chunk[64];
    int updated = 0;

    while (1) {
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
                if (handle_line(s_line, fields)) {
                    updated = 1;
                }
                s_len = 0;
                continue;
            }
            if (s_len + 1 < sizeof(s_line)) {
                s_line[s_len++] = c;
            } else {
                // overflow — reset
                s_len = 0;
            }
        }
    }
    return updated;
}

/* USB Serial/JTAG can accept bytes into soft/HW buffers and still not push
 * them to the Mac until the TX FIFO is flushed. Clearing pending on a soft
 * "success" without a flush drops SET lines while the panel already updated. */
static int write_all(const void *data, size_t n, TickType_t timeout)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t left = n;
    TickType_t deadline = xTaskGetTickCount() + timeout;
    while (left > 0) {
        TickType_t now = xTaskGetTickCount();
        TickType_t slice = (deadline > now) ? (deadline - now) : 1;
        int wrote = usb_serial_jtag_write_bytes(p, left, slice);
        if (wrote <= 0) {
            return 0;
        }
        p += (size_t)wrote;
        left -= (size_t)wrote;
    }
    usb_serial_jtag_ll_txfifo_flush();
    return 1;
}

static int write_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return 0;
    }
    size_t n = strlen(line);
    if (!write_all(line, n, pdMS_TO_TICKS(100))) {
        return 0;
    }
    const char nl = '\n';
    return write_all(&nl, 1, pdMS_TO_TICKS(40));
}

int serial_model_send_set_model(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    char buf[SERIAL_LINE_MAX];
    int n = snprintf(buf, sizeof(buf), "SET MODEL %s", name);
    if (n <= 0 || n >= (int)sizeof(buf)) {
        return 0;
    }
    int ok = write_line(buf);
    if (ok) {
        ESP_LOGI(TAG, "TX %s", buf);
    } else {
        ESP_LOGW(TAG, "TX failed %s", buf);
    }
    return ok;
}

int serial_model_send_set_thinking(const char *level)
{
    if (level == NULL || level[0] == '\0') {
        return 0;
    }
    char buf[SERIAL_LINE_MAX];
    int n = snprintf(buf, sizeof(buf), "SET THINKING %s", level);
    if (n <= 0 || n >= (int)sizeof(buf)) {
        return 0;
    }
    int ok = write_line(buf);
    if (ok) {
        ESP_LOGI(TAG, "TX %s", buf);
    } else {
        ESP_LOGW(TAG, "TX failed %s", buf);
    }
    return ok;
}
