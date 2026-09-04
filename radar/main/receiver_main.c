#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "display_profile.h"
#include "display_sync.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "radar_link.h"
#include "radar_view.h"
#include "receiver_touch.h"
#include "touch_calibration.h"

enum {
    PIN_LCD_MOSI = 8,
    PIN_LCD_DC = 9,
    PIN_LCD_RST = 10,
    PIN_LCD_CS = 11,
    PIN_LCD_MISO = 16,
    PIN_BACKLIGHT = 17,
    PIN_LCD_SCLK = 18,
    LCD_TRANSFER_ROWS = 32,
    RENDER_INTERVAL_MS = 33,
    PING_INTERVAL_MS = 2000,
    FRAME_TIMEOUT_MS = 1500,
    DISPLAY_IDLE_MS = 10000,
    TOUCH_POLL_INTERVAL_MS = 20,
    TOUCH_HOLD_MS = 3000,
};

#define LCD_HOST SPI2_HOST

static const char *TAG = "receiver";
static esp_lcd_panel_handle_t s_panel;
static bool s_display_asleep;

static void init_display(void)
{
    const gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 0));

    const spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_TRANSFER_ROWS * sizeof(uint16_t) + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t panel_io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                              &io_config, &panel_io));
    ESP_ERROR_CHECK(display_sync_init(panel_io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(display_profile_create_panel(panel_io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(display_profile_apply_view_orientation(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 1));
}

static void display_sleep(void)
{
    if (s_display_asleep) {
        return;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, false));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 0));
    s_display_asleep = true;
    ESP_LOGI(TAG, "display blanked after inactivity");
}

static void display_wake(void)
{
    if (!s_display_asleep) {
        return;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 1));
    radar_view_restore(s_panel);
    s_display_asleep = false;
    ESP_LOGI(TAG, "display awake");
}

static bool copy_received_people(radar_view_t *view, radar_link_frame_t *frame,
                                 bool *has_person)
{
    if (!radar_link_receive(frame)) {
        return false;
    }

    radar_person_t people[RADAR_VIEW_MAX_PEOPLE] = {0};
    *has_person = false;
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        people[index] = (radar_person_t) {
            .x_mm = frame->targets[index].x_mm,
            .y_mm = frame->targets[index].y_mm,
            .active = frame->targets[index].active,
        };
        *has_person = *has_person || people[index].active;
    }
    radar_view_set_people(view, people, frame->radial_acceleration_mm_per_second_squared);
    return true;
}

static void clear_people(radar_view_t *view)
{
    const radar_person_t people[RADAR_VIEW_MAX_PEOPLE] = {0};
    radar_view_set_people(view, people, 0);
}

static void release_sleep_holds(void)
{
    gpio_deep_sleep_hold_dis();
    const gpio_num_t pins[] = {
        RECEIVER_TOUCH_INT_GPIO,
        RECEIVER_TOUCH_RESET_GPIO,
        PIN_LCD_RST,
        PIN_BACKLIGHT,
    };
    for (size_t index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        gpio_hold_dis(pins[index]);
        rtc_gpio_hold_dis(pins[index]);
    }
    rtc_gpio_deinit(RECEIVER_TOUCH_INT_GPIO);
    rtc_gpio_deinit(RECEIVER_TOUCH_RESET_GPIO);
}

static void hold_digital_low(gpio_num_t pin)
{
    ESP_ERROR_CHECK(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(pin, 0));
    ESP_ERROR_CHECK(gpio_hold_en(pin));
}

static void hold_rtc_output(gpio_num_t pin, int level)
{
    ESP_ERROR_CHECK(gpio_set_level(pin, level));
    ESP_ERROR_CHECK(rtc_gpio_init(pin));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_set_level(pin, level));
    ESP_ERROR_CHECK(rtc_gpio_hold_en(pin));
}

static void wait_for_touch_release(void)
{
    while (gpio_get_level(RECEIVER_TOUCH_INT_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void configure_touch_wakeup(void)
{
    ESP_ERROR_CHECK(rtc_gpio_init(RECEIVER_TOUCH_INT_GPIO));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(RECEIVER_TOUCH_INT_GPIO,
                                           RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(RECEIVER_TOUCH_INT_GPIO));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(RECEIVER_TOUCH_INT_GPIO));
    hold_rtc_output(RECEIVER_TOUCH_RESET_GPIO, 1);
    hold_digital_low(PIN_BACKLIGHT);
    gpio_deep_sleep_hold_en();
}

static void enter_deep_sleep(touch_hold_action_t action)
{
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, false));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 0));
    wait_for_touch_release();

    if (action == TOUCH_HOLD_POWER_OFF) {
        // The receiver has no accessible EN switch, so POWER OFF must remain
        // recoverable from the touch interrupt. Hold only the LCD reset low.
        hold_digital_low(PIN_LCD_RST);
        ESP_LOGI(TAG, "POWER OFF: tap the screen to wake");
    } else {
        ESP_LOGI(TAG, "SLEEP: tap the screen to wake");
    }
    configure_touch_wakeup();
    wait_for_touch_release();

    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
    ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(RECEIVER_TOUCH_INT_GPIO, 0));
    esp_deep_sleep_start();
}

static void calibration_hold(touch_hold_action_t action)
{
    enter_deep_sleep(action);
}

void app_main(void)
{
    ESP_LOGI(TAG, "radar display receiver start");
    bool woke_from_touch_sleep =
        (esp_sleep_get_wakeup_causes() & (1UL << ESP_SLEEP_WAKEUP_EXT0)) != 0;
    release_sleep_holds();
    init_display();
    ESP_ERROR_CHECK(receiver_touch_init());
    ESP_ERROR_CHECK(radar_link_init());

    radar_view_t view;
    radar_view_start(s_panel, &view);
    radar_link_frame_t frame = {0};
    touch_calibration_t calibration = {0};
    TickType_t now = xTaskGetTickCount();
    TickType_t last_render = now - pdMS_TO_TICKS(RENDER_INTERVAL_MS);
    TickType_t last_ping = 0;
    TickType_t last_frame = now;
    TickType_t last_presence = now;
    bool have_frame = false;
    bool touch_down = false;
    bool ignore_touch_until_release = woke_from_touch_sleep;
    TickType_t touch_started_at = 0;

    while (true) {
        now = xTaskGetTickCount();
        bool has_person = false;
        bool received = copy_received_people(&view, &frame, &has_person);
        if (received) {
            have_frame = true;
            last_frame = now;
            if (has_person) {
                last_presence = now;
                if (s_display_asleep) {
                    display_wake();
                }
            }
            if (last_ping == 0
                || now - last_ping >= pdMS_TO_TICKS(PING_INTERVAL_MS)) {
                radar_view_trigger_ping(&view);
                last_ping = now;
            }
        } else if (have_frame && now - last_frame >= pdMS_TO_TICKS(FRAME_TIMEOUT_MS)) {
            clear_people(&view);
            have_frame = false;
            last_ping = 0;
        }

        if (!s_display_asleep && now - last_presence >= pdMS_TO_TICKS(DISPLAY_IDLE_MS)) {
            display_sleep();
        }

        uint16_t raw_x;
        uint16_t raw_y;
        bool touched = receiver_touch_read(&raw_x, &raw_y);
        if (touched) {
            if (s_display_asleep) {
                display_wake();
                last_presence = now;
                ignore_touch_until_release = true;
                touch_started_at = 0;
            } else if (!ignore_touch_until_release) {
                if (!touch_down) {
                    touch_started_at = now;
                }
                if (touch_started_at != 0
                    && now - touch_started_at >= pdMS_TO_TICKS(TOUCH_HOLD_MS)) {
                    ESP_LOGI(TAG, "CALIBRATION HOLD: three seconds");
                    touch_calibration_run(s_panel, receiver_touch_read,
                                           calibration_hold, PIN_BACKLIGHT,
                                           &calibration);
                    ESP_ERROR_CHECK(gpio_set_level(PIN_BACKLIGHT, 1));
                    radar_view_restore(s_panel);
                    last_presence = xTaskGetTickCount();
                    touch_down = true;
                    ignore_touch_until_release = true;
                    touch_started_at = 0;
                }
            }
            touch_down = true;
        } else {
            touch_down = false;
            touch_started_at = 0;
            ignore_touch_until_release = false;
        }

        if (now - last_render >= pdMS_TO_TICKS(RENDER_INTERVAL_MS)) {
            if (!s_display_asleep) {
                radar_view_step(s_panel, &view);
            }
            last_render = now;
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_INTERVAL_MS));
    }
}
