#include "ld2450.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    LD2450_UART = UART_NUM_1,
    LD2450_TX_GPIO = 42,
    LD2450_RX_GPIO = 41,
    LD2450_BAUD_RATE = 256000,
    LD2450_FRAME_BYTES = 30,
    LD2450_RX_BUFFER_BYTES = 2048,
    LD2450_COMMAND_TIMEOUT_MS = 500,
    LD2450_MAX_COMMAND_BYTES = 16,
    LD2450_MAX_RESPONSE_BYTES = 64,
};

static const char *TAG = "ld2450";
static const uint8_t s_header[] = {0xaa, 0xff, 0x03, 0x00};
static uint8_t s_frame[LD2450_FRAME_BYTES];
static size_t s_frame_position;
static bool s_logged_uart_activity;

static bool wait_for_data_frame(uint32_t timeout_ms);

static esp_err_t send_command(uint16_t command, const uint8_t *value,
                              size_t value_length)
{
    uint8_t frame[LD2450_MAX_COMMAND_BYTES] = {
        0xfd, 0xfc, 0xfb, 0xfa,
    };
    size_t frame_length = 4 + 2 + 2 + value_length + 4;
    if (frame_length > sizeof(frame)) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t data_length = 2 + value_length;
    frame[4] = data_length & 0xff;
    frame[5] = data_length >> 8;
    frame[6] = command & 0xff;
    frame[7] = command >> 8;
    for (size_t index = 0; index < value_length; ++index) {
        frame[8 + index] = value[index];
    }
    size_t footer = 8 + value_length;
    frame[footer + 0] = 0x04;
    frame[footer + 1] = 0x03;
    frame[footer + 2] = 0x02;
    frame[footer + 3] = 0x01;

    int written = uart_write_bytes(LD2450_UART, frame, frame_length);
    if (written != (int)frame_length) {
        return ESP_FAIL;
    }
    return uart_wait_tx_done(LD2450_UART, pdMS_TO_TICKS(100));
}

static esp_err_t wait_for_ack(uint16_t command, uint8_t *response_data,
                              size_t response_size)
{
    static const uint8_t header[] = {0xfd, 0xfc, 0xfb, 0xfa};
    static const uint8_t footer[] = {0x04, 0x03, 0x02, 0x01};
    uint8_t response[LD2450_MAX_RESPONSE_BYTES];
    size_t position = 0;
    size_t expected_length = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(LD2450_COMMAND_TIMEOUT_MS);

    while (xTaskGetTickCount() - start < timeout) {
        uint8_t byte;
        int count = uart_read_bytes(LD2450_UART, &byte, 1, pdMS_TO_TICKS(20));
        if (count != 1) {
            continue;
        }

        if (position < sizeof(header)) {
            if (byte == header[position]) {
                response[position++] = byte;
            } else {
                position = byte == header[0] ? 1 : 0;
                if (position == 1) {
                    response[0] = byte;
                }
            }
            continue;
        }

        response[position++] = byte;
        if (position == 6) {
            uint16_t data_length = (uint16_t)response[4]
                                 | ((uint16_t)response[5] << 8);
            expected_length = 4 + 2 + data_length + sizeof(footer);
            if (data_length < 4 || expected_length > sizeof(response)) {
                position = 0;
                expected_length = 0;
            }
            continue;
        }
        if (!expected_length || position < expected_length) {
            continue;
        }

        size_t footer_position = expected_length - sizeof(footer);
        uint16_t ack_command = (uint16_t)response[6]
                             | ((uint16_t)response[7] << 8);
        uint16_t status = (uint16_t)response[8]
                        | ((uint16_t)response[9] << 8);
        bool valid_footer = memcmp(&response[footer_position], footer,
                                   sizeof(footer)) == 0;
        if (valid_footer && ack_command == (command | 0x0100)) {
            if (status != 0) {
                return ESP_FAIL;
            }
            if (response_size > 0) {
                uint16_t data_length = (uint16_t)response[4]
                                     | ((uint16_t)response[5] << 8);
                if (!response_data || data_length < 4 + response_size) {
                    return ESP_ERR_INVALID_RESPONSE;
                }
                memcpy(response_data, &response[10], response_size);
            }
            return ESP_OK;
        }
        position = 0;
        expected_length = 0;
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t run_command(uint16_t command, const uint8_t *value,
                             size_t value_length, uint8_t *response_data,
                             size_t response_size)
{
    esp_err_t error = send_command(command, value, value_length);
    return error == ESP_OK
         ? wait_for_ack(command, response_data, response_size)
         : error;
}

static esp_err_t detect_baud_rate(int *detected_baud_rate)
{
    static const int baud_rates[] = {
        256000, 115200, 230400, 460800, 9600, 19200, 38400, 57600,
    };
    static const uint8_t protocol_version[] = {0x01, 0x00};

    for (size_t index = 0; index < sizeof(baud_rates) / sizeof(baud_rates[0]); ++index) {
        int baud_rate = baud_rates[index];
        esp_err_t error = uart_set_baudrate(LD2450_UART, baud_rate);
        if (error != ESP_OK) {
            return error;
        }
        uart_flush_input(LD2450_UART);
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "probing %d baud", baud_rate);

        if (wait_for_data_frame(250)) {
            *detected_baud_rate = baud_rate;
            return ESP_OK;
        }

        error = run_command(0x00ff, protocol_version,
                            sizeof(protocol_version), NULL, 0);
        if (error != ESP_OK) {
            continue;
        }

        esp_err_t exit_error = run_command(0x00fe, NULL, 0, NULL, 0);
        if (exit_error != ESP_OK) {
            return exit_error;
        }
        uart_flush_input(LD2450_UART);
        *detected_baud_rate = baud_rate;
        return ESP_OK;
    }

    uart_set_baudrate(LD2450_UART, LD2450_BAUD_RATE);
    uart_flush_input(LD2450_UART);
    return ESP_ERR_NOT_FOUND;
}

static int16_t decode_signed_magnitude(const uint8_t *bytes)
{
    uint16_t raw = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    int16_t magnitude = raw & 0x7fff;
    return (raw & 0x8000) ? magnitude : -magnitude;
}

static void parse_frame(ld2450_frame_t *frame)
{
    for (size_t index = 0; index < LD2450_MAX_TARGETS; ++index) {
        const uint8_t *target = &s_frame[4 + index * 8];
        uint16_t resolution = (uint16_t)target[6] | ((uint16_t)target[7] << 8);
        frame->targets[index] = (ld2450_target_t) {
            .x_mm = decode_signed_magnitude(&target[0]),
            .y_mm = decode_signed_magnitude(&target[2]),
            .speed_cm_s = decode_signed_magnitude(&target[4]),
            .resolution_mm = resolution,
            .active = target[0] || target[1] || target[2] || target[3]
                   || target[4] || target[5] || target[6] || target[7],
        };
    }
}

static bool consume_byte(uint8_t byte, ld2450_frame_t *frame)
{
    if (s_frame_position < sizeof(s_header)) {
        if (byte == s_header[s_frame_position]) {
            s_frame[s_frame_position++] = byte;
        } else {
            s_frame_position = byte == s_header[0] ? 1 : 0;
            if (s_frame_position == 1) {
                s_frame[0] = byte;
            }
        }
        return false;
    }

    s_frame[s_frame_position++] = byte;
    if (s_frame_position < LD2450_FRAME_BYTES) {
        return false;
    }

    bool valid = s_frame[28] == 0x55 && s_frame[29] == 0xcc;
    s_frame_position = 0;
    if (valid) {
        parse_frame(frame);
    }
    return valid;
}

static bool wait_for_data_frame(uint32_t timeout_ms)
{
    ld2450_frame_t frame = {0};
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    s_frame_position = 0;

    while (xTaskGetTickCount() - start < timeout) {
        uint8_t bytes[64];
        int count = uart_read_bytes(LD2450_UART, bytes, sizeof(bytes),
                                    pdMS_TO_TICKS(20));
        for (int index = 0; index < count; ++index) {
            if (consume_byte(bytes[index], &frame)) {
                ESP_LOGI(TAG, "valid target-data frame detected");
                return true;
            }
        }
    }

    s_frame_position = 0;
    return false;
}

esp_err_t ld2450_init(void)
{
    const uart_config_t config = {
        .baud_rate = LD2450_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t error = uart_driver_install(LD2450_UART, LD2450_RX_BUFFER_BYTES,
                                          0, 0, NULL, 0);
    if (error == ESP_OK) {
        error = uart_param_config(LD2450_UART, &config);
    }
    if (error == ESP_OK) {
        error = uart_set_pin(LD2450_UART, LD2450_TX_GPIO, LD2450_RX_GPIO,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (error != ESP_OK) {
        uart_driver_delete(LD2450_UART);
        return error;
    }

    int detected_baud_rate = 0;
    error = detect_baud_rate(&detected_baud_rate);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "LD2450 not found at any supported baud rate");
        uart_driver_delete(LD2450_UART);
        return error;
    }
    ESP_LOGI(TAG, "UART ready: ESP TX=GPIO%d ESP RX=GPIO%d at %d baud",
             LD2450_TX_GPIO, LD2450_RX_GPIO, detected_baud_rate);
    return ESP_OK;
}

esp_err_t ld2450_set_single_target_mode(void)
{
    static const uint8_t protocol_version[] = {0x01, 0x00};
    uart_flush_input(LD2450_UART);

    esp_err_t error = run_command(0x00ff, protocol_version,
                                  sizeof(protocol_version), NULL, 0);
    if (error == ESP_OK) {
        error = run_command(0x0080, NULL, 0, NULL, 0);
    }

    uint8_t tracking_mode_data[2] = {0};
    if (error == ESP_OK) {
        error = run_command(0x0091, NULL, 0, tracking_mode_data,
                            sizeof(tracking_mode_data));
        uint16_t tracking_mode = (uint16_t)tracking_mode_data[0]
                               | ((uint16_t)tracking_mode_data[1] << 8);
        if (error == ESP_OK && tracking_mode != 0x0001) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }

    esp_err_t exit_error = run_command(0x00fe, NULL, 0, NULL, 0);
    if (error == ESP_OK) {
        error = exit_error;
    }
    uart_flush_input(LD2450_UART);

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "single-target tracking confirmed (mode=0x%02x%02x)",
                 tracking_mode_data[1], tracking_mode_data[0]);
    } else {
        ESP_LOGE(TAG, "single-target configuration failed: %s", esp_err_to_name(error));
    }
    return error;
}

esp_err_t ld2450_get_firmware_version(ld2450_firmware_version_t *version)
{
    if (!version) {
        return ESP_ERR_INVALID_ARG;
    }

    static const uint8_t protocol_version[] = {0x01, 0x00};
    uint8_t version_data[8] = {0};
    uart_flush_input(LD2450_UART);

    esp_err_t error = run_command(0x00ff, protocol_version,
                                  sizeof(protocol_version), NULL, 0);
    if (error == ESP_OK) {
        error = run_command(0x00a0, NULL, 0, version_data,
                            sizeof(version_data));
    }
    esp_err_t exit_error = run_command(0x00fe, NULL, 0, NULL, 0);
    if (error == ESP_OK) {
        error = exit_error;
    }
    uart_flush_input(LD2450_UART);
    if (error != ESP_OK) {
        return error;
    }

    uint16_t major_minor = (uint16_t)version_data[2]
                         | ((uint16_t)version_data[3] << 8);
    version->major = major_minor >> 8;
    version->minor = major_minor & 0xff;
    version->build = (uint32_t)version_data[4]
                   | ((uint32_t)version_data[5] << 8)
                   | ((uint32_t)version_data[6] << 16)
                   | ((uint32_t)version_data[7] << 24);
    ESP_LOGI(TAG, "firmware V%u.%02u.%08" PRIx32,
             version->major, version->minor, version->build);
    return ESP_OK;
}

bool ld2450_poll(ld2450_frame_t *frame)
{
    if (!frame) {
        return false;
    }

    uint8_t bytes[256];
    int count = uart_read_bytes(LD2450_UART, bytes, sizeof(bytes), 0);
    if (count > 0 && !s_logged_uart_activity) {
        ESP_LOGI(TAG, "UART electrical activity detected (%d bytes in first read)", count);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, bytes, count < 30 ? count : 30, ESP_LOG_INFO);
        s_logged_uart_activity = true;
    }
    bool received_frame = false;
    for (int index = 0; index < count; ++index) {
        if (consume_byte(bytes[index], frame)) {
            received_frame = true;
        }
    }
    return received_frame;
}
