#include "radar_link.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

enum {
    RADAR_LINK_CHANNEL = 6,
    RADAR_LINK_MAGIC = 0x52414452,
    RADAR_LINK_VERSION = 1,
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t target_count;
    uint16_t sequence;
    struct __attribute__((packed)) {
        int16_t x_mm;
        int16_t y_mm;
        uint8_t active;
    } targets[RADAR_LINK_MAX_TARGETS];
} radar_link_packet_t;

static const char *TAG = "radar_link";
#if defined(RADAR_LINK_ROLE_TRANSMITTER)
static const uint8_t s_broadcast_address[ESP_NOW_ETH_ALEN] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
#endif

#if defined(RADAR_LINK_ROLE_RECEIVER)
static portMUX_TYPE s_receive_lock = portMUX_INITIALIZER_UNLOCKED;
static radar_link_frame_t s_received_frame;
static bool s_frame_pending;

static void radar_link_received(const esp_now_recv_info_t *info,
                                const uint8_t *data, int data_length)
{
    (void)info;
    if (data_length != sizeof(radar_link_packet_t)) {
        return;
    }

    radar_link_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != RADAR_LINK_MAGIC || packet.version != RADAR_LINK_VERSION
        || packet.target_count != RADAR_LINK_MAX_TARGETS) {
        return;
    }

    radar_link_frame_t frame = {0};
    for (int index = 0; index < RADAR_LINK_MAX_TARGETS; ++index) {
        frame.targets[index] = (radar_link_target_t) {
            .x_mm = packet.targets[index].x_mm,
            .y_mm = packet.targets[index].y_mm,
            .active = packet.targets[index].active != 0,
        };
    }

    portENTER_CRITICAL(&s_receive_lock);
    s_received_frame = frame;
    s_frame_pending = true;
    portEXIT_CRITICAL(&s_receive_lock);
}
#endif

static esp_err_t initialize_wifi(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        error = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(error, TAG, "initialize NVS failed");

    error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "initialize Wi-Fi failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "set Wi-Fi storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "set Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(RADAR_LINK_CHANNEL, WIFI_SECOND_CHAN_NONE),
                        TAG, "set ESP-NOW channel failed");
    return ESP_OK;
}

esp_err_t radar_link_init(void)
{
    ESP_RETURN_ON_ERROR(initialize_wifi(), TAG, "Wi-Fi setup failed");
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "ESP-NOW initialization failed");

#if defined(RADAR_LINK_ROLE_TRANSMITTER)
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_broadcast_address, ESP_NOW_ETH_ALEN);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = RADAR_LINK_CHANNEL;
    peer.encrypt = false;
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "add broadcast peer failed");
#elif defined(RADAR_LINK_ROLE_RECEIVER)
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(radar_link_received), TAG,
                        "register receive callback failed");
#endif

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG,
                        "read station MAC failed");
    ESP_LOGI(TAG, "%s ready on channel %d, MAC " MACSTR,
             radar_link_role_name(), RADAR_LINK_CHANNEL, MAC2STR(mac));
    return ESP_OK;
}

esp_err_t radar_link_send(const radar_link_frame_t *frame)
{
#if defined(RADAR_LINK_ROLE_TRANSMITTER)
    static uint16_t sequence;
    radar_link_packet_t packet = {
        .magic = RADAR_LINK_MAGIC,
        .version = RADAR_LINK_VERSION,
        .target_count = RADAR_LINK_MAX_TARGETS,
        .sequence = ++sequence,
    };
    for (int index = 0; index < RADAR_LINK_MAX_TARGETS; ++index) {
        packet.targets[index].x_mm = frame->targets[index].x_mm;
        packet.targets[index].y_mm = frame->targets[index].y_mm;
        packet.targets[index].active = frame->targets[index].active ? 1 : 0;
    }
    return esp_now_send(s_broadcast_address, (const uint8_t *)&packet, sizeof(packet));
#else
    (void)frame;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool radar_link_receive(radar_link_frame_t *frame)
{
#if defined(RADAR_LINK_ROLE_RECEIVER)
    bool received = false;
    portENTER_CRITICAL(&s_receive_lock);
    if (s_frame_pending) {
        *frame = s_received_frame;
        s_frame_pending = false;
        received = true;
    }
    portEXIT_CRITICAL(&s_receive_lock);
    return received;
#else
    (void)frame;
    return false;
#endif
}

const char *radar_link_role_name(void)
{
#if defined(RADAR_LINK_ROLE_TRANSMITTER)
    return "ESP-NOW transmitter";
#elif defined(RADAR_LINK_ROLE_RECEIVER)
    return "ESP-NOW receiver";
#else
    return "local radar only";
#endif
}
