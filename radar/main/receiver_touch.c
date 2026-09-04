#include "receiver_touch.h"

#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    TOUCH_I2C_PORT = I2C_NUM_0,
    TOUCH_I2C_ADDRESS = 0x38,
    TOUCH_REGISTER_POINTS = 0x02,
};

static const char *TAG = "receiver_touch";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;

static esp_err_t read_register(uint8_t reg, uint8_t *data, size_t length)
{
    if (!s_device) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_device, &reg, 1, data, length, 100);
}

esp_err_t receiver_touch_init(void)
{
    const gpio_config_t reset = {
        .pin_bit_mask = 1ULL << RECEIVER_TOUCH_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t error = gpio_config(&reset);
    if (error != ESP_OK) {
        return error;
    }
    ESP_ERROR_CHECK(gpio_set_level(RECEIVER_TOUCH_RESET_GPIO, 0));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(gpio_set_level(RECEIVER_TOUCH_RESET_GPIO, 1));
    vTaskDelay(pdMS_TO_TICKS(250));

    const gpio_config_t interrupt = {
        .pin_bit_mask = 1ULL << RECEIVER_TOUCH_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&interrupt);
    if (error != ESP_OK) {
        return error;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = RECEIVER_TOUCH_SDA_GPIO,
        .scl_io_num = RECEIVER_TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    error = i2c_new_master_bus(&bus_config, &s_bus);
    if (error != ESP_OK) {
        return error;
    }
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    error = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (error != ESP_OK) {
        return error;
    }

    ESP_LOGI(TAG, "FT6336 touch ready on SDA=%d SCL=%d INT=%d",
             RECEIVER_TOUCH_SDA_GPIO, RECEIVER_TOUCH_SCL_GPIO,
             RECEIVER_TOUCH_INT_GPIO);
    return ESP_OK;
}

bool receiver_touch_read(uint16_t *raw_x, uint16_t *raw_y)
{
    if (!raw_x || !raw_y) {
        return false;
    }

    uint8_t data[5] = {0};
    if (read_register(TOUCH_REGISTER_POINTS, data, sizeof(data)) != ESP_OK
        || (data[0] & 0x0f) == 0) {
        return false;
    }
    *raw_x = ((uint16_t)(data[1] & 0x0f) << 8) | data[2];
    *raw_y = ((uint16_t)(data[3] & 0x0f) << 8) | data[4];
    return true;
}
