#include "speech_recognition.h"

#include <stdbool.h>

#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microphone.h"
#include "model_path.h"

enum {
    SPEECH_TASK_STACK_BYTES = 8192,
    SPEECH_TASK_PRIORITY = 5,
    SPEECH_COMMAND_TIMEOUT_MS = 4000,
};

typedef struct {
    voice_command_t id;
    const char *phrase;
} speech_command_definition_t;

static const char *TAG = "speech";
static const speech_command_definition_t s_commands[] = {
    { VOICE_COMMAND_UP, "up" },
    { VOICE_COMMAND_DOWN, "down" },
    { VOICE_COMMAND_LEFT, "left" },
    { VOICE_COMMAND_RIGHT, "right" },
    { VOICE_COMMAND_UP_RIGHT, "up right" },
    { VOICE_COMMAND_UP_LEFT, "up left" },
    { VOICE_COMMAND_DOWN_RIGHT, "down right" },
    { VOICE_COMMAND_DOWN_LEFT, "down left" },
    { VOICE_COMMAND_RESET, "reset" },
};

static srmodel_list_t *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_multinet;
static model_iface_data_t *s_multinet_data;
static TaskHandle_t s_feed_task;
static TaskHandle_t s_detect_task;
static portMUX_TYPE s_event_lock = portMUX_INITIALIZER_UNLOCKED;
static voice_command_t s_pending_command;
static bool s_sound_detected;

static void publish_command(voice_command_t command)
{
    portENTER_CRITICAL(&s_event_lock);
    s_pending_command = command;
    portEXIT_CRITICAL(&s_event_lock);
}

static void publish_sound_detected(void)
{
    portENTER_CRITICAL(&s_event_lock);
    s_sound_detected = true;
    portEXIT_CRITICAL(&s_event_lock);
}

static void speech_feed_task(void *unused)
{
    int input_chunk_size = s_afe->get_feed_chunksize(s_afe_data);
    int16_t *samples = heap_caps_malloc(input_chunk_size * sizeof(*samples),
                                        MALLOC_CAP_8BIT);
    if (!samples) {
        heap_caps_free(samples);
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        size_t samples_read = 0;
        if (microphone_read_pcm16(samples, input_chunk_size, &samples_read,
                                  pdMS_TO_TICKS(100)) != ESP_OK
            || samples_read != (size_t)input_chunk_size) {
            continue;
        }

        if (microphone_sound_detected_pcm(samples, samples_read)) {
            publish_sound_detected();
        }
        s_afe->feed(s_afe_data, samples);
    }
}

static void speech_detect_task(void *unused)
{
    int multinet_chunk_size = s_multinet->get_samp_chunksize(s_multinet_data);
    ESP_LOGI(TAG, "listening for eight directions plus reset");
    while (true) {
        afe_fetch_result_t *audio = s_afe->fetch(s_afe_data);
        if (!audio || audio->ret_value != ESP_OK
            || audio->data_size < multinet_chunk_size * (int)sizeof(int16_t)) {
            continue;
        }
        esp_mn_state_t state = s_multinet->detect(s_multinet_data, audio->data);
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *result = s_multinet->get_results(s_multinet_data);
            if (result && result->num > 0
                && result->command_id[0] > VOICE_COMMAND_NONE
                && result->command_id[0] <= VOICE_COMMAND_RESET) {
                voice_command_t command = result->command_id[0];
                ESP_LOGI(TAG, "recognized command=%d phrase=%s", command, result->string);
                publish_command(command);
            }
            s_multinet->clean(s_multinet_data);
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            s_multinet->clean(s_multinet_data);
        }
    }
}

esp_err_t speech_recognition_init(void)
{
    s_models = esp_srmodel_init("model");
    if (!s_models) {
        return ESP_FAIL;
    }

    char *model_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!model_name) {
        return ESP_ERR_NOT_FOUND;
    }
    s_multinet = esp_mn_handle_from_name(model_name);
    s_multinet_data = s_multinet ? s_multinet->create(model_name, SPEECH_COMMAND_TIMEOUT_MS) : NULL;
    if (!s_multinet_data) {
        return ESP_FAIL;
    }

    esp_err_t error = esp_mn_commands_alloc(s_multinet, s_multinet_data);
    for (size_t command = 0; error == ESP_OK && command < sizeof(s_commands) / sizeof(s_commands[0]); ++command) {
        error = esp_mn_commands_add(s_commands[command].id, s_commands[command].phrase);
    }
    if (error != ESP_OK || esp_mn_commands_update()) {
        return ESP_FAIL;
    }

    afe_config_t *config = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!config) {
        return ESP_ERR_NO_MEM;
    }
    config->wakenet_init = false;
    config->agc_init = true;
    config->agc_mode = AFE_AGC_MODE_WEBRTC;
    config->agc_compression_gain_db = 18;
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    config->fixed_output_channel = true;
    s_afe = esp_afe_handle_from_config(config);
    s_afe_data = s_afe ? s_afe->create_from_config(config) : NULL;
    afe_config_free(config);
    if (!s_afe_data) {
        return ESP_FAIL;
    }

    s_multinet->set_det_threshold(s_multinet_data, 0.35f);
    if (xTaskCreatePinnedToCore(speech_feed_task, "speech_feed", SPEECH_TASK_STACK_BYTES,
                                NULL, SPEECH_TASK_PRIORITY + 1, &s_feed_task, 1) != pdPASS
        || xTaskCreatePinnedToCore(speech_detect_task, "speech_detect", SPEECH_TASK_STACK_BYTES,
                                   NULL, SPEECH_TASK_PRIORITY, &s_detect_task, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

voice_command_t speech_recognition_take_command(void)
{
    portENTER_CRITICAL(&s_event_lock);
    voice_command_t command = s_pending_command;
    s_pending_command = VOICE_COMMAND_NONE;
    portEXIT_CRITICAL(&s_event_lock);
    return command;
}

bool speech_recognition_take_sound_detected(void)
{
    portENTER_CRITICAL(&s_event_lock);
    bool detected = s_sound_detected;
    s_sound_detected = false;
    portEXIT_CRITICAL(&s_event_lock);
    return detected;
}
