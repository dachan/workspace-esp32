#include "encoder.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * Two KY-040-style encoders on Lonely Binary N16R8 (see s3-n16r8.jpeg).
 * Thinking: CLK41 DT40 SW39
 * Model:    CLK1  DT2  SW42
 *
 * Thinking: polled falling CLK, DT direction. Two detents per level. Pulses
 * are held until the knob pauses so a fast flick is one burst (Light↔Extra
 * High) and a slow pair of detents is one level. Emitting each pulse
 * immediately would paint the panel and miss the rest of the turn.
 *
 * Model: PCNT hardware quadrature. Polling could not decode this knob — the
 * same loop also runs a full 480x320 SPI flush, so DT was sampled long after
 * CLK fell, read its resting level every time, and every detent looked like
 * the same direction. The list then walked to one end and stayed there. PCNT
 * counts both lines in hardware, so a blocked loop cannot lose or misread a
 * detent. Eight counts (two mechanical detents) plus a 160 ms gap per model
 * step so the dial is less twitchy; the list clamps at the ends of the
 * active catalog (ChatGPT or Cursor).
 */
#if defined(AI_MODEL_PROFILE_SUPERMINI)
static const int s_clk[ENCODER_COUNT] = {4, 1};
static const int s_dt[ENCODER_COUNT] = {5, 2};
static const int s_sw[ENCODER_COUNT] = {6, 7};
#else
static const int s_clk[ENCODER_COUNT] = {41, 1};
static const int s_dt[ENCODER_COUNT] = {40, 2};
static const int s_sw[ENCODER_COUNT] = {39, 42};
#endif

static const char *TAG = "encoder";

static int s_last_clk[ENCODER_COUNT];
static int s_last_dir[ENCODER_COUNT];
static int64_t s_last_step_us[ENCODER_COUNT];
static int s_pulses[ENCODER_COUNT];
static int s_sign[ENCODER_COUNT];
static int s_last_sw[ENCODER_COUNT];
static int64_t s_last_sw_us[ENCODER_COUNT];
static int s_sw_armed[ENCODER_COUNT];

#define MIN_STEP_US 20000
#define DIR_LOCK_US 100000
#define THINKING_PULSES_PER_STEP 2
/* Emit after the knob pauses so a flick is not split by a display flush. */
#define THINKING_BURST_IDLE_US 80000

/* Model (PCNT) — less sensitive: two detents + emit gap per model step. */
#define MODEL_PCNT_LIMIT 1000
#define MODEL_PCNT_RESET 500
#define MODEL_COUNTS_PER_STEP 8
#define MODEL_EMIT_US 160000
/* Reverse the model knob's physical direction in the catalog. */
#define MODEL_SIGN -1

static pcnt_unit_handle_t s_model_unit;
static int s_model_prev_count;
static int s_model_acc;
static int s_model_sign;
static int64_t s_model_emit_us;

#if defined(AI_MODEL_PROFILE_SUPERMINI)
static pcnt_unit_handle_t s_think_unit;
static int s_think_prev_count;
static int s_think_acc;
static int s_think_sign;
static int64_t s_think_last_us;
static volatile int s_pending_delta[ENCODER_COUNT];
static volatile int s_pending_sw[ENCODER_COUNT];
static volatile int s_thinking_hold;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static void encoder_task(void *arg);
#endif

static esp_err_t pcnt_setup(int clk, int dt, pcnt_unit_handle_t *unit)
{
    pcnt_unit_config_t unit_cfg = {
        .low_limit = -MODEL_PCNT_LIMIT,
        .high_limit = MODEL_PCNT_LIMIT,
    };
    esp_err_t err = pcnt_new_unit(&unit_cfg, unit);
    if (err != ESP_OK) {
        return err;
    }
    /* KY-040 contacts are noisy; drop sub-microsecond spikes in hardware. */
    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = 1000,
    };
    err = pcnt_unit_set_glitch_filter(*unit, &filter_cfg);
    if (err != ESP_OK) {
        return err;
    }

    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num = clk,
        .level_gpio_num = dt,
    };
    pcnt_channel_handle_t chan_a = NULL;
    err = pcnt_new_channel(*unit, &chan_a_cfg, &chan_a);
    if (err != ESP_OK) {
        return err;
    }
    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num = dt,
        .level_gpio_num = clk,
    };
    pcnt_channel_handle_t chan_b = NULL;
    err = pcnt_new_channel(*unit, &chan_b_cfg, &chan_b);
    if (err != ESP_OK) {
        return err;
    }

    /* Count all four quadrature edges so direction comes from both lines. */
    err = pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                      PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK) {
        return err;
    }
    err = pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                       PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK) {
        return err;
    }
    err = pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                      PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (err != ESP_OK) {
        return err;
    }
    err = pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK) {
        return err;
    }

    err = pcnt_unit_enable(*unit);
    if (err != ESP_OK) {
        return err;
    }
    err = pcnt_unit_clear_count(*unit);
    if (err != ESP_OK) {
        return err;
    }
    err = pcnt_unit_start(*unit);
    if (err != ESP_OK) {
        return err;
    }

    /* PCNT reconfigures its pins, so re-arm the module pull-ups afterwards. */
    gpio_set_pull_mode(clk, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(dt, GPIO_PULLUP_ONLY);
    return ESP_OK;
}

static esp_err_t model_pcnt_init(void)
{
    esp_err_t err = pcnt_setup(s_clk[ENCODER_MODEL], s_dt[ENCODER_MODEL], &s_model_unit);
    if (err != ESP_OK) {
        return err;
    }
    s_model_prev_count = 0;
    s_model_acc = 0;
    s_model_sign = 0;
    s_model_emit_us = 0;
    return ESP_OK;
}

esp_err_t encoder_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < ENCODER_COUNT; i++) {
        const int pins[] = {s_clk[i], s_dt[i], s_sw[i]};
        for (int j = 0; j < 3; j++) {
            gpio_reset_pin(pins[j]);
            if (rtc_gpio_is_valid_gpio(pins[j])) {
                rtc_gpio_deinit(pins[j]);
            }
        }
        mask |= (1ULL << s_clk[i]) | (1ULL << s_dt[i]) | (1ULL << s_sw[i]);
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < ENCODER_COUNT; i++) {
        s_last_clk[i] = gpio_get_level(s_clk[i]);
        s_last_dir[i] = 0;
        s_last_step_us[i] = 0;
        s_pulses[i] = 0;
        s_sign[i] = 0;
        s_last_sw[i] = gpio_get_level(s_sw[i]);
        s_sw_armed[i] = 1;
        s_last_sw_us[i] = 0;
    }

    err = model_pcnt_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "model PCNT init failed: %s", esp_err_to_name(err));
        return err;
    }
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    err = pcnt_setup(s_clk[ENCODER_THINKING], s_dt[ENCODER_THINKING], &s_think_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "thinking PCNT init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_think_prev_count = 0;
    s_think_acc = 0;
    s_think_sign = 0;
    s_think_last_us = 0;
    ESP_LOGI(TAG, "thinking CLK%d=%d DT%d=%d SW%d=%d",
             s_clk[ENCODER_THINKING], gpio_get_level(s_clk[ENCODER_THINKING]),
             s_dt[ENCODER_THINKING], gpio_get_level(s_dt[ENCODER_THINKING]),
             s_sw[ENCODER_THINKING], gpio_get_level(s_sw[ENCODER_THINKING]));
    ESP_LOGI(TAG, "model CLK%d=%d DT%d=%d SW%d=%d",
             s_clk[ENCODER_MODEL], gpio_get_level(s_clk[ENCODER_MODEL]),
             s_dt[ENCODER_MODEL], gpio_get_level(s_dt[ENCODER_MODEL]),
             s_sw[ENCODER_MODEL], gpio_get_level(s_sw[ENCODER_MODEL]));
    if (xTaskCreate(encoder_task, "enc", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "encoder task");
        return ESP_ERR_NO_MEM;
    }
#endif
    return ESP_OK;
}

static int model_delta(void)
{
    if (s_model_unit == NULL) {
        return 0;
    }
    int count = 0;
    if (pcnt_unit_get_count(s_model_unit, &count) != ESP_OK) {
        return 0;
    }
    int delta_counts = count - s_model_prev_count;
    s_model_prev_count = count;
    if (count > MODEL_PCNT_RESET || count < -MODEL_PCNT_RESET) {
        if (pcnt_unit_clear_count(s_model_unit) == ESP_OK) {
            s_model_prev_count = 0;
        }
    }
    if (delta_counts != 0) {
        int sign = delta_counts > 0 ? 1 : -1;
        if (sign != s_model_sign) {
            s_model_sign = sign;
            s_model_acc = delta_counts;
        } else {
            s_model_acc += delta_counts;
        }
    }

    /* Do not consume acc until the emit gap allows a step — otherwise fast
     * turns burn counts during the gap and never update the panel/SET path. */
    int64_t now = esp_timer_get_time();
    if (now - s_model_emit_us < MODEL_EMIT_US) {
        return 0;
    }
    if (s_model_acc >= MODEL_COUNTS_PER_STEP) {
        s_model_acc -= MODEL_COUNTS_PER_STEP;
        s_model_emit_us = now;
        return MODEL_SIGN;
    }
    if (s_model_acc <= -MODEL_COUNTS_PER_STEP) {
        s_model_acc += MODEL_COUNTS_PER_STEP;
        s_model_emit_us = now;
        return -MODEL_SIGN;
    }
    return 0;
}

#if defined(AI_MODEL_PROFILE_SUPERMINI)
static int thinking_delta(void)
{
    if (s_think_unit == NULL) {
        return 0;
    }
    int count = 0;
    if (pcnt_unit_get_count(s_think_unit, &count) != ESP_OK) {
        return 0;
    }
    int delta_counts = count - s_think_prev_count;
    s_think_prev_count = count;
    if (count > MODEL_PCNT_RESET || count < -MODEL_PCNT_RESET) {
        if (pcnt_unit_clear_count(s_think_unit) == ESP_OK) {
            s_think_prev_count = 0;
        }
    }
    int64_t now = esp_timer_get_time();
    if (delta_counts != 0) {
        s_think_last_us = now;
        int sign = delta_counts > 0 ? 1 : -1;
        if (sign != s_think_sign) {
            s_think_sign = sign;
            s_think_acc = delta_counts;
        } else {
            s_think_acc += delta_counts;
        }
    }
    s_thinking_hold = s_think_acc != 0
        && (now - s_think_last_us) < THINKING_BURST_IDLE_US;
    if (s_think_acc == 0 || now - s_think_last_us < THINKING_BURST_IDLE_US) {
        return 0;
    }
    int steps = 0;
    if (s_think_acc >= MODEL_COUNTS_PER_STEP) {
        steps = s_think_acc / MODEL_COUNTS_PER_STEP;
        s_think_acc %= MODEL_COUNTS_PER_STEP;
    } else if (s_think_acc <= -MODEL_COUNTS_PER_STEP) {
        steps = -((-s_think_acc) / MODEL_COUNTS_PER_STEP);
        s_think_acc %= MODEL_COUNTS_PER_STEP;
    }
    return steps;
}
#endif

static int poll_sw(encoder_id_t id)
{
    int sw = gpio_get_level(s_sw[id]);
    int64_t now = esp_timer_get_time();
    int pressed = 0;
    if (s_last_sw[id] == 1 && sw == 0 && s_sw_armed[id]) {
        if (now - s_last_sw_us[id] > 250000) {
            pressed = 1;
            s_last_sw_us[id] = now;
            s_sw_armed[id] = 0;
        }
    }
    if (sw == 1) {
        s_sw_armed[id] = 1;
    }
    s_last_sw[id] = sw;
    return pressed;
}

#if !defined(AI_MODEL_PROFILE_SUPERMINI)
static int poll_thinking_gpio(void)
{
    int clk = gpio_get_level(s_clk[ENCODER_THINKING]);
    int dt = gpio_get_level(s_dt[ENCODER_THINKING]);
    int delta = 0;
    if (s_last_clk[ENCODER_THINKING] == 1 && clk == 0) {
        delta = (dt == 1) ? 1 : -1;
    }
    s_last_clk[ENCODER_THINKING] = clk;
    int64_t now = esp_timer_get_time();
    bool accepted = delta != 0 && now - s_last_step_us[ENCODER_THINKING] >= MIN_STEP_US;
    if (s_last_dir[ENCODER_THINKING] != 0 && delta != s_last_dir[ENCODER_THINKING]
        && now - s_last_step_us[ENCODER_THINKING] < DIR_LOCK_US) {
        accepted = false;
    }
    if (accepted) {
        s_last_dir[ENCODER_THINKING] = delta;
        s_last_step_us[ENCODER_THINKING] = now;
        if (delta != s_sign[ENCODER_THINKING]) {
            s_sign[ENCODER_THINKING] = delta;
            s_pulses[ENCODER_THINKING] = 0;
        }
        s_pulses[ENCODER_THINKING]++;
    }

    if (s_pulses[ENCODER_THINKING] < THINKING_PULSES_PER_STEP
        || now - s_last_step_us[ENCODER_THINKING] < THINKING_BURST_IDLE_US) {
        return 0;
    }
    int steps = s_pulses[ENCODER_THINKING] / THINKING_PULSES_PER_STEP;
    s_pulses[ENCODER_THINKING] %= THINKING_PULSES_PER_STEP;
    int sign = s_sign[ENCODER_THINKING];
    s_last_dir[ENCODER_THINKING] = 0;
    return sign * steps;
}
#endif

#if defined(AI_MODEL_PROFILE_SUPERMINI)
static void encoder_task(void *arg)
{
    (void)arg;
    while (1) {
        int thinking = thinking_delta();
        int model = model_delta();
        int think_sw = poll_sw(ENCODER_THINKING);
        int model_sw = poll_sw(ENCODER_MODEL);
        if (thinking || model || think_sw || model_sw) {
            ESP_LOGI(TAG, "step think=%+d model=%+d sw=%d/%d",
                     thinking, model, think_sw, model_sw);
        }
        portENTER_CRITICAL(&s_lock);
        s_pending_delta[ENCODER_THINKING] += thinking;
        s_pending_delta[ENCODER_MODEL] += model;
        if (think_sw) {
            s_pending_sw[ENCODER_THINKING] = 1;
        }
        if (model_sw) {
            s_pending_sw[ENCODER_MODEL] = 1;
        }
        portEXIT_CRITICAL(&s_lock);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
#endif

int encoder_delta(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return 0;
    }
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    portENTER_CRITICAL(&s_lock);
    int delta = s_pending_delta[id];
    s_pending_delta[id] = 0;
    portEXIT_CRITICAL(&s_lock);
    return delta;
#else
    if (id == ENCODER_MODEL) {
        return model_delta();
    }
    return poll_thinking_gpio();
#endif
}

int encoder_hold_paint(void)
{
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    return s_thinking_hold;
#else
    if (s_pulses[ENCODER_THINKING] == 0) {
        return 0;
    }
    return (esp_timer_get_time() - s_last_step_us[ENCODER_THINKING])
        < THINKING_BURST_IDLE_US;
#endif
}

int encoder_button_pressed(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return 0;
    }
#if defined(AI_MODEL_PROFILE_SUPERMINI)
    portENTER_CRITICAL(&s_lock);
    int pressed = s_pending_sw[id];
    s_pending_sw[id] = 0;
    portEXIT_CRITICAL(&s_lock);
    return pressed;
#else
    return poll_sw(id);
#endif
}
