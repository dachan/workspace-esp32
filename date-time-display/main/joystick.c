#include "joystick.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"

/* The module's COM pin is tied to GND. Each switch is therefore active-low. */
static const gpio_num_t s_pins[] = {
    GPIO_NUM_13, /* UP */
    GPIO_NUM_14, /* DOWN */
    GPIO_NUM_15, /* LEFT */
    GPIO_NUM_16, /* RIGHT */
    GPIO_NUM_17, /* MID */
    GPIO_NUM_18, /* SEL */
    GPIO_NUM_4,  /* RST */
};

static const joystick_action_t s_actions[] = {
    JOYSTICK_UP,
    JOYSTICK_DOWN,
    JOYSTICK_LEFT,
    JOYSTICK_RIGHT,
    JOYSTICK_MID,
    JOYSTICK_SEL,
    JOYSTICK_RST,
};

static uint8_t s_raw;
static uint8_t s_stable;
static uint8_t s_same_count[sizeof(s_pins) / sizeof(s_pins[0])];
static bool s_ready;

esp_err_t joystick_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (size_t i = 0; i < sizeof(s_pins) / sizeof(s_pins[0]); ++i) {
        config.pin_bit_mask |= 1ULL << s_pins[i];
    }
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    s_raw = 0xff;
    s_stable = 0xff;
    for (size_t i = 0; i < sizeof(s_same_count) / sizeof(s_same_count[0]); ++i) {
        s_same_count[i] = 0;
    }
    s_ready = true;
    return ESP_OK;
}

joystick_action_t joystick_poll(void)
{
    if (!s_ready) {
        return JOYSTICK_NONE;
    }
    for (size_t i = 0; i < sizeof(s_pins) / sizeof(s_pins[0]); ++i) {
        uint8_t bit = (uint8_t)(1u << i);
        bool high = gpio_get_level(s_pins[i]) != 0;
        bool old_raw_high = (s_raw & bit) != 0;
        if (high != old_raw_high) {
            if (high) {
                s_raw |= bit;
            } else {
                s_raw &= (uint8_t)~bit;
            }
            s_same_count[i] = 0;
            continue;
        }
        if (s_same_count[i] < 2) {
            s_same_count[i]++;
        }
        if (s_same_count[i] < 2) {
            continue;
        }

        bool stable_high = (s_stable & bit) != 0;
        if (high == stable_high) {
            continue;
        }
        if (high) {
            s_stable |= bit;
        } else {
            s_stable &= (uint8_t)~bit;
            return s_actions[i];
        }
    }
    return JOYSTICK_NONE;
}
