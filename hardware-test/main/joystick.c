#include "joystick.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_log.h"

enum {
    JOYSTICK_VRX_CHANNEL = ADC_CHANNEL_1, // GPIO12
    JOYSTICK_VRY_CHANNEL = ADC_CHANNEL_2, // GPIO13
    CENTER_SAMPLES = 16,
    READ_SAMPLES = 4,
    DEADZONE = 260,
    JOYSTICK_SWITCH_GPIO = GPIO_NUM_14,
};

static const char *TAG = "joystick";

static esp_err_t read_average(adc_oneshot_unit_handle_t adc, adc_channel_t channel,
                              int samples, int *result)
{
    int total = 0;
    for (int sample = 0; sample < samples; ++sample) {
        int reading = 0;
        esp_err_t error = adc_oneshot_read(adc, channel, &reading);
        if (error != ESP_OK) {
            return error;
        }
        total += reading;
    }
    *result = total / samples;
    return ESP_OK;
}

static int cursor_step(int value)
{
    int magnitude = abs(value);
    if (magnitude <= DEADZONE) {
        return 0;
    }

    return value < 0 ? -1 : 1;
}

esp_err_t joystick_init(joystick_t *joystick)
{
    *joystick = (joystick_t) {0};

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t error = adc_oneshot_new_unit(&unit_config, &joystick->adc);
    if (error != ESP_OK) {
        return error;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    error = adc_oneshot_config_channel(joystick->adc, JOYSTICK_VRX_CHANNEL,
                                       &channel_config);
    if (error == ESP_OK) {
        error = adc_oneshot_config_channel(joystick->adc, JOYSTICK_VRY_CHANNEL,
                                           &channel_config);
    }
    if (error != ESP_OK) {
        adc_oneshot_del_unit(joystick->adc);
        joystick->adc = NULL;
        return error;
    }

    gpio_config_t switch_config = {
        .pin_bit_mask = 1ULL << JOYSTICK_SWITCH_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&switch_config);
    if (error != ESP_OK) {
        adc_oneshot_del_unit(joystick->adc);
        joystick->adc = NULL;
        return error;
    }

    error = read_average(joystick->adc, JOYSTICK_VRX_CHANNEL, CENTER_SAMPLES,
                         &joystick->center_x);
    if (error == ESP_OK) {
        error = read_average(joystick->adc, JOYSTICK_VRY_CHANNEL, CENTER_SAMPLES,
                             &joystick->center_y);
    }
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "GPIO12/13 center=(%d,%d); GPIO14 switch idle=%d",
                 joystick->center_x, joystick->center_y,
                 gpio_get_level(JOYSTICK_SWITCH_GPIO));
    }
    return error;
}

esp_err_t joystick_read(joystick_t *joystick, joystick_input_t *input)
{
    int raw_x = 0;
    int raw_y = 0;
    esp_err_t error = read_average(joystick->adc, JOYSTICK_VRX_CHANNEL, READ_SAMPLES, &raw_x);
    if (error != ESP_OK) {
        return error;
    }
    error = read_average(joystick->adc, JOYSTICK_VRY_CHANNEL, READ_SAMPLES, &raw_y);
    if (error != ESP_OK) {
        return error;
    }

    int offset_x = raw_x - joystick->center_x;
    int offset_y = raw_y - joystick->center_y;
    // Landscape MADCTL already matches viewed axes. Do not swap or negate.
    *input = (joystick_input_t) {
        .cursor_dx = cursor_step(offset_x),
        .cursor_dy = cursor_step(offset_y),
        .switch_pressed = !joystick->switch_latched
                       && gpio_get_level(JOYSTICK_SWITCH_GPIO) == 0,
    };
    joystick->switch_latched = gpio_get_level(JOYSTICK_SWITCH_GPIO) == 0;
    return ESP_OK;
}
