#include "clock_sync.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"

static const char *TAG = "clock-sync";
static const char *const CLOCK_NAMESPACE = "clock";
static const char *const WIFI_SSID_KEY = "wifi_ssid";
static const char *const WIFI_PASSWORD_KEY = "wifi_password";
static const char *const NTP_SERVER = "time.cloudflare.com";
static clock_sync_time_saved_cb_t s_time_saved;
static bool s_network_started;
static bool s_time_sync_started;

static bool load_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size) {
    nvs_handle_t handle;
    if (nvs_open(CLOCK_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t stored_ssid_size = ssid_size, stored_password_size = password_size;
    esp_err_t ssid_err = nvs_get_str(handle, WIFI_SSID_KEY, ssid, &stored_ssid_size);
    esp_err_t password_err = nvs_get_str(handle, WIFI_PASSWORD_KEY, password, &stored_password_size);
    nvs_close(handle);
    return ssid_err == ESP_OK && password_err == ESP_OK && ssid[0] != '\0';
}
static void save_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t handle;
    if (nvs_open(CLOCK_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_str(handle, WIFI_SSID_KEY, ssid); nvs_set_str(handle, WIFI_PASSWORD_KEY, password);
    nvs_commit(handle); nvs_close(handle);
}
static void clear_wifi_credentials(void) {
    nvs_handle_t handle;
    if (nvs_open(CLOCK_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_erase_key(handle, WIFI_SSID_KEY); nvs_erase_key(handle, WIFI_PASSWORD_KEY);
    nvs_commit(handle); nvs_close(handle);
}
static void time_synced(struct timeval *time_value) {
    ESP_LOGI(TAG, "NTP time synchronized");
    if (s_time_saved != NULL) s_time_saved(time_value->tv_sec);
}
static void start_sntp(void) {
    if (s_time_sync_started) return;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(time_synced);
    esp_sntp_init(); s_time_sync_started = true;
    ESP_LOGI(TAG, "requesting time from %s", NTP_SERVER);
}
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) start_sntp();
}
static void start_network(void) {
    char ssid[sizeof(((wifi_config_t *)0)->sta.ssid)] = {0};
    char password[sizeof(((wifi_config_t *)0)->sta.password)] = {0};
    if (!load_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "Wi-Fi is not configured; use WIFI <ssid> <password> over USB serial"); return;
    }
    if (!s_network_started) {
        ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta(); wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));
        s_network_started = true;
    }
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
void clock_sync_init(clock_sync_time_saved_cb_t time_saved) { s_time_saved = time_saved; start_network(); }
bool clock_sync_handle_command(const char *command, void (*write_line)(const char *)) {
    if (strcmp(command, "WIFI CLEAR") == 0) { clear_wifi_credentials(); write_line("WIFI credentials cleared; restart to disconnect\n"); return true; }
    if (strcmp(command, "WIFI STATUS") == 0) { write_line(s_network_started ? "WIFI configured; connection in progress\n" : "WIFI not configured\n"); return true; }
    if (strncmp(command, "WIFI ", 5) != 0) return false;
    char ssid[sizeof(((wifi_config_t *)0)->sta.ssid)] = {0}, password[sizeof(((wifi_config_t *)0)->sta.password)] = {0};
    if (sscanf(command + 5, "%32s %64s", ssid, password) != 2) { write_line("Usage: WIFI <ssid> <password> (spaces are not supported)\n"); return true; }
    save_wifi_credentials(ssid, password);
    write_line("WIFI credentials saved; restart the board to connect and sync time\n");
    return true;
}
