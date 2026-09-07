#include "encoder.h"

#include "driver/gpio.h"
#include "esp_timer.h"

/*
 * Two KY-040-style encoders on Lonely Binary N16R8 (see s3-n16r8.jpeg).
 * Display SPI uses the LEFT header (4,8-11,16-18). Encoders use the RIGHT
 * header so wiring stays on the free side of the board.
 *
 * Thinking: CLK41 DT40 SW39
 * Model:    CLK1  DT2  SW42
 *
 * Decode full quadrature (not CLK-edge only). Fast spins used to emit a
 * bounce the other way ("overscroll flips the setting").
 */
static const int s_clk[ENCODER_COUNT] = {41, 1};
static const int s_dt[ENCODER_COUNT] = {40, 2};
static const int s_sw[ENCODER_COUNT] = {39, 42};

static uint8_t s_prev_ab[ENCODER_COUNT];
static int s_accum[ENCODER_COUNT];
static int64_t s_last_step_us[ENCODER_COUNT];
static int s_last_sw[ENCODER_COUNT];
static int64_t s_last_sw_us[ENCODER_COUNT];
static int s_sw_armed[ENCODER_COUNT];

/* One KY-040 detent is typically 4 Gray-code steps. */
#define DETENT_STEPS 4
/* Ignore steps closer than this (contact bounce / chatter). */
#define MIN_STEP_US 1500

static inline uint8_t read_ab(int id)
{
    int a = gpio_get_level(s_clk[id]);
    int b = gpio_get_level(s_dt[id]);
    return (uint8_t)((a << 1) | b);
}

esp_err_t encoder_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < ENCODER_COUNT; i++) {
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
        s_prev_ab[i] = read_ab(i);
        s_accum[i] = 0;
        s_last_step_us[i] = 0;
        s_last_sw[i] = gpio_get_level(s_sw[i]);
        s_sw_armed[i] = 1;
        s_last_sw_us[i] = 0;
    }
    return ESP_OK;
}

int encoder_delta(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return 0;
    }

    uint8_t curr = read_ab(id);
    uint8_t prev = s_prev_ab[id];
    if (curr == prev) {
        return 0;
    }
    s_prev_ab[id] = curr;

    /* Only accept single-bit Gray transitions; drop illegal bounce states. */
    int step = 0;
    switch ((prev << 2) | curr) {
    case 0b0001: /* 00 -> 01 */
    case 0b0111: /* 01 -> 11 */
    case 0b1110: /* 11 -> 10 */
    case 0b1000: /* 10 -> 00 */
        step = +1;
        break;
    case 0b0010: /* 00 -> 10 */
    case 0b1011: /* 10 -> 11 */
    case 0b1101: /* 11 -> 01 */
    case 0b0100: /* 01 -> 00 */
        step = -1;
        break;
    default:
        /* 2-bit jump / bounce — ignore */
        return 0;
    }

    s_accum[id] += step;
    int delta = 0;
    if (s_accum[id] >= DETENT_STEPS) {
        s_accum[id] -= DETENT_STEPS;
        delta = +1;
    } else if (s_accum[id] <= -DETENT_STEPS) {
        s_accum[id] += DETENT_STEPS;
        delta = -1;
    }
    if (delta == 0) {
        return 0;
    }

    int64_t now = esp_timer_get_time();
    if (now - s_last_step_us[id] < MIN_STEP_US) {
        /* Too soon after last detent — treat as bounce, keep accum cleared. */
        return 0;
    }
    s_last_step_us[id] = now;
    return delta;
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

void encoder_clear_partial(encoder_id_t id)
{
    if (id < 0 || id >= ENCODER_COUNT) {
        return;
    }
    s_accum[id] = 0;
}
