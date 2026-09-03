#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ld2450.h"
#include "radar_link.h"
#include "radar_motion_leds.h"

static const char *TAG = "radar";

enum {
    MOTION_MAX_ACCELERATION_MM_PER_SECOND_SQUARED = 10000,
    MOTION_MAX_SAMPLE_GAP_MS = 1000,
};

typedef struct {
    bool initialized;
    int nearest_target_index;
    uint32_t previous_distance_mm;
    float previous_radial_speed_mm_per_second;
    float radial_acceleration_mm_per_second_squared;
    TickType_t observed_at;
} motion_tracker_t;

static void copy_radar_frame(const ld2450_frame_t *radar_frame,
                             radar_link_frame_t *link_frame)
{
    for (int index = 0; index < LD2450_MAX_TARGETS; ++index) {
        link_frame->targets[index] = (radar_link_target_t) {
            .x_mm = radar_frame->targets[index].x_mm,
            .y_mm = radar_frame->targets[index].y_mm,
            .active = radar_frame->targets[index].active,
        };
    }
}

static void log_targets(const ld2450_frame_t *frame)
{
    int target_count = 0;
    for (int index = 0; index < LD2450_MAX_TARGETS; ++index) {
        target_count += frame->targets[index].active ? 1 : 0;
    }

    ESP_LOGI(TAG,
             "LD2450 targets=%d t1=(%d,%d) t2=(%d,%d) t3=(%d,%d) mm",
             target_count,
             frame->targets[0].x_mm, frame->targets[0].y_mm,
             frame->targets[1].x_mm, frame->targets[1].y_mm,
             frame->targets[2].x_mm, frame->targets[2].y_mm);
}

static uint32_t integer_square_root(uint32_t value)
{
    uint32_t result = 0;
    uint32_t bit = 1u << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static bool nearest_distance_mm(const ld2450_frame_t *frame, uint32_t *distance_mm,
                                int *target_index)
{
    uint64_t nearest_squared = UINT64_MAX;
    int nearest_index = -1;
    for (int index = 0; index < LD2450_MAX_TARGETS; ++index) {
        const ld2450_target_t *target = &frame->targets[index];
        if (!target->active) {
            continue;
        }

        int64_t x_mm = target->x_mm;
        int64_t y_mm = target->y_mm;
        uint64_t distance_squared = (uint64_t)(x_mm * x_mm + y_mm * y_mm);
        if (distance_squared < nearest_squared) {
            nearest_squared = distance_squared;
            nearest_index = index;
        }
    }
    if (nearest_index < 0) {
        return false;
    }

    *distance_mm = nearest_squared > UINT32_MAX
                 ? UINT16_MAX : integer_square_root((uint32_t)nearest_squared);
    *target_index = nearest_index;
    return true;
}

static int16_t rounded_acceleration(float acceleration)
{
    return acceleration < 0.0f ? -(int16_t)(-acceleration + 0.5f)
                               : (int16_t)(acceleration + 0.5f);
}

static int16_t update_motion_acceleration(const ld2450_frame_t *frame,
                                           motion_tracker_t *tracker, TickType_t now)
{
    uint32_t distance_mm;
    int target_index;
    if (!nearest_distance_mm(frame, &distance_mm, &target_index)) {
        tracker->initialized = false;
        tracker->radial_acceleration_mm_per_second_squared = 0.0f;
        return 0;
    }

    uint32_t elapsed_ms = pdTICKS_TO_MS(now - tracker->observed_at);
    if (!tracker->initialized || target_index != tracker->nearest_target_index
        || elapsed_ms == 0 || elapsed_ms > MOTION_MAX_SAMPLE_GAP_MS) {
        tracker->initialized = true;
        tracker->nearest_target_index = target_index;
        tracker->previous_distance_mm = distance_mm;
        tracker->previous_radial_speed_mm_per_second = 0.0f;
        tracker->radial_acceleration_mm_per_second_squared = 0.0f;
        tracker->observed_at = now;
        return 0;
    }

    float radial_speed = ((int32_t)distance_mm - (int32_t)tracker->previous_distance_mm)
                       * 1000.0f / elapsed_ms;
    float acceleration = (radial_speed - tracker->previous_radial_speed_mm_per_second)
                       * 1000.0f / elapsed_ms;
    if (acceleration > MOTION_MAX_ACCELERATION_MM_PER_SECOND_SQUARED) {
        acceleration = MOTION_MAX_ACCELERATION_MM_PER_SECOND_SQUARED;
    } else if (acceleration < -MOTION_MAX_ACCELERATION_MM_PER_SECOND_SQUARED) {
        acceleration = -MOTION_MAX_ACCELERATION_MM_PER_SECOND_SQUARED;
    }

    tracker->radial_acceleration_mm_per_second_squared =
        (tracker->radial_acceleration_mm_per_second_squared * 3.0f + acceleration) / 4.0f;
    tracker->previous_distance_mm = distance_mm;
    tracker->previous_radial_speed_mm_per_second = radial_speed;
    tracker->observed_at = now;
    return rounded_acceleration(tracker->radial_acceleration_mm_per_second_squared);
}

void app_main(void)
{
    ESP_LOGI(TAG, "headless LD2450 ESP-NOW transmitter start");

    ESP_ERROR_CHECK(ld2450_init());
    ESP_ERROR_CHECK(radar_link_init());
    ESP_ERROR_CHECK(radar_motion_leds_init());

    esp_err_t tracking_error = ld2450_set_single_target_mode();
    if (tracking_error != ESP_OK) {
        // Valid target frames can still arrive when the command path times out.
        ESP_LOGW(TAG, "single-target command: %s; continuing to read target frames",
                 esp_err_to_name(tracking_error));
    }

    ld2450_frame_t radar_frame = {0};
    radar_link_frame_t link_frame = {0};
    motion_tracker_t motion_tracker = {0};
    TickType_t last_report = 0;

    while (true) {
        if (ld2450_poll(&radar_frame)) {
            TickType_t now = xTaskGetTickCount();
            copy_radar_frame(&radar_frame, &link_frame);
            link_frame.radial_acceleration_mm_per_second_squared =
                update_motion_acceleration(&radar_frame, &motion_tracker, now);
            radar_motion_leds_update_acceleration(
                link_frame.radial_acceleration_mm_per_second_squared);
            esp_err_t send_error = radar_link_send(&link_frame);
            if (send_error != ESP_OK && send_error != ESP_ERR_ESPNOW_NO_MEM) {
                ESP_LOGW(TAG, "ESP-NOW broadcast failed: %s", esp_err_to_name(send_error));
            }
            if (now - last_report >= pdMS_TO_TICKS(1000)) {
                log_targets(&radar_frame);
                last_report = now;
            }
        }
        radar_motion_leds_step();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
