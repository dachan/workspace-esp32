#include "encoder.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_timer.h"

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
 * step so the dial is less twitchy; the list clamps at Astra / GPT-5.5.
 */
static const int s_clk[ENCODER_COUNT] = {41, 1};
static const int s_dt[ENCODER_COUNT] = {40, 2};
static const int s_sw[ENCODER_COUNT] = {39, 42};

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
/* +1 counterclockwise → GPT-5.5. Flip to -1 if the two directions land swapped. */
#define MODEL_SIGN 1

static pcnt_unit_handle_t s_model_unit;
static int s_model_prev_count;
static int s_model_acc;
static int s_model_sign;
static int64_t s_model_emit_us;


static esp_err_t model_pcnt_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .low_limit = -MODEL_PCNT_LIMIT,
        .high_limit = MODEL_PCNT_LIMIT,
    };
    esp_err_t err = pcnt_new_unit(&unit_cfg, &s_model_unit);
    if (err != ESP_OK) {
        return err;
    }
    /* KY-040 contacts are noisy; drop sub-microsecond spikes in hardware. */
    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = 1000,
    };
    err = pcnt_unit_set_glitch_filter(s_model_unit, &filter_cfg);
    if (err != ESP_OK) {
        return err;
    }

    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num = s_clk[ENCODER_MODEL],
        .level_gpio_num = s_dt[ENCODER_MODEL],
    };
    pcnt_channel_handle_t chan_a = NULL;
    err = pcnt_new_channel(s_model_unit, &chan_a_cfg, &chan_a);
    if (err != ESP_OK) {
        return err;
    }
    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num = s_dt[ENCODER_MODEL],
        .level_gpio_num = s_clk[ENCODER_MODEL],
    };
    pcnt_channel_handle_t chan_b = NULL;
    err = pcnt_new_channel(s_model_unit, &chan_b_cfg, &chan_b);
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

    err = pcnt_unit_enable(s_model_unit);
    if (err != ESP_OK) {
        return err;
    }
    err = pcnt_unit_clear_count(s_model_unit);
    if (err != ESP_OK) {
        return err;
    }
    err = pcnt_unit_start(s_model_unit);
    if (err != ESP_OK) {
        return err;
    }

    /* PCNT reconfigures its pins, so re-arm the module pull-ups afterwards. */
    gpio_set_pull_mode(s_clk[ENCODER_MODEL], GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(s_dt[ENCODER_MODEL], GPIO_PULLUP_ONLY);

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

int encoder_delta(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return 0;
    }
    if (id == ENCODER_MODEL) {
        return model_delta();
    }

    int clk = gpio_get_level(s_clk[id]);
    int dt = gpio_get_level(s_dt[id]);
    int delta = 0;
    if (s_last_clk[id] == 1 && clk == 0) {
        delta = (dt == 1) ? 1 : -1;
    }
    s_last_clk[id] = clk;
    int64_t now = esp_timer_get_time();
    bool accepted = delta != 0 && now - s_last_step_us[id] >= MIN_STEP_US;
    if (s_last_dir[id] != 0 && delta != s_last_dir[id]
        && now - s_last_step_us[id] < DIR_LOCK_US) {
        accepted = false;
    }
    if (accepted) {
        s_last_dir[id] = delta;
        s_last_step_us[id] = now;
        if (delta != s_sign[id]) {
            s_sign[id] = delta;
            s_pulses[id] = 0;
        }
        s_pulses[id]++;
    }

    if (s_pulses[id] < THINKING_PULSES_PER_STEP
        || now - s_last_step_us[id] < THINKING_BURST_IDLE_US) {
        return 0;
    }
    int steps = s_pulses[id] / THINKING_PULSES_PER_STEP;
    s_pulses[id] %= THINKING_PULSES_PER_STEP;
    int sign = s_sign[id];
    s_last_dir[id] = 0;
    return sign * steps;
}

int encoder_hold_paint(void)
{
    if (s_pulses[ENCODER_THINKING] == 0) {
        return 0;
    }
    return (esp_timer_get_time() - s_last_step_us[ENCODER_THINKING])
        < THINKING_BURST_IDLE_US;
}

int encoder_button_pressed(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return 0;
    }
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
