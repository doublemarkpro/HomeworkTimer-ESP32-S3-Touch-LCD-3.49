#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "wifi_manager";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_manager_status_t s_status;
static wifi_manager_network_t s_networks[WIFI_MANAGER_MAX_NETWORKS];
static size_t s_network_count;
static bool s_should_connect;
static uint8_t s_retry_count;

static void set_state(wifi_manager_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.generation++;
    portEXIT_CRITICAL(&s_lock);
}

static void update_scan_results(void)
{
    uint16_t found = 0;
    if (esp_wifi_scan_get_ap_num(&found) != ESP_OK) {
        set_state(WIFI_MANAGER_FAILED);
        return;
    }
    if (found > 16) {
        found = 16;
    }
    wifi_ap_record_t records[16] = {0};
    uint16_t count = found;
    if (count > 0 && esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        set_state(WIFI_MANAGER_FAILED);
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_network_count = 0;
    for (uint16_t index = 0; index < count && s_network_count < WIFI_MANAGER_MAX_NETWORKS;
         ++index) {
        if (records[index].ssid[0] == '\0') {
            continue;
        }
        bool duplicate = false;
        for (size_t existing = 0; existing < s_network_count; ++existing) {
            if (strncmp(s_networks[existing].ssid, (const char *)records[index].ssid,
                        sizeof(s_networks[existing].ssid)) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        wifi_manager_network_t *network = &s_networks[s_network_count++];
        strlcpy(network->ssid, (const char *)records[index].ssid, sizeof(network->ssid));
        network->rssi = records[index].rssi;
        network->secured = records[index].authmode != WIFI_AUTH_OPEN;
    }
    s_status.state = s_status.ip[0] != '\0' ? WIFI_MANAGER_CONNECTED : WIFI_MANAGER_IDLE;
    s_status.generation++;
    portEXIT_CRITICAL(&s_lock);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        update_scan_results();
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *connected = event_data;
        portENTER_CRITICAL(&s_lock);
        strlcpy(s_status.ssid, (const char *)connected->ssid, sizeof(s_status.ssid));
        s_status.rssi = 0;
        s_retry_count = 0;
        s_status.generation++;
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&s_lock);
        s_status.ip[0] = '\0';
        if (s_should_connect && s_retry_count < 5) {
            s_retry_count++;
            s_status.state = WIFI_MANAGER_CONNECTING;
        } else if (s_should_connect) {
            s_should_connect = false;
            s_status.state = WIFI_MANAGER_FAILED;
        } else {
            s_status.state = WIFI_MANAGER_IDLE;
        }
        const bool reconnect = s_should_connect;
        s_status.generation++;
        portEXIT_CRITICAL(&s_lock);
        if (reconnect) {
            esp_wifi_connect();
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&got_ip->ip_info.ip));
        wifi_ap_record_t ap = {0};
        esp_wifi_sta_get_ap_info(&ap);
        portENTER_CRITICAL(&s_lock);
        strlcpy(s_status.ip, ip, sizeof(s_status.ip));
        s_status.rssi = ap.rssi;
        s_status.state = WIFI_MANAGER_CONNECTED;
        s_retry_count = 0;
        s_status.generation++;
        portEXIT_CRITICAL(&s_lock);
    }
}

esp_err_t wifi_manager_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = WIFI_MANAGER_OFF;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    wifi_event_handler, NULL),
                        TAG, "wifi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    wifi_event_handler, NULL),
                        TAG, "IP event handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    wifi_config_t saved = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0] != '\0') {
        portENTER_CRITICAL(&s_lock);
        strlcpy(s_status.ssid, (const char *)saved.sta.ssid, sizeof(s_status.ssid));
        s_status.state = WIFI_MANAGER_CONNECTING;
        s_should_connect = true;
        s_status.generation++;
        portEXIT_CRITICAL(&s_lock);
        esp_wifi_connect();
    } else {
        set_state(WIFI_MANAGER_IDLE);
    }
    return ESP_OK;
}

esp_err_t wifi_manager_scan(void)
{
    const wifi_manager_state_t previous = s_status.state;
    set_state(WIFI_MANAGER_SCANNING);
    const esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        set_state(previous);
    }
    return err;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    if (password != NULL) {
        strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    }
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    s_should_connect = false;
    s_retry_count = 0;
    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "save wifi config");
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_status.ssid, ssid, sizeof(s_status.ssid));
    s_status.ip[0] = '\0';
    s_status.state = WIFI_MANAGER_CONNECTING;
    s_should_connect = true;
    s_status.generation++;
    portEXIT_CRITICAL(&s_lock);
    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect(void)
{
    s_should_connect = false;
    s_retry_count = 0;
    wifi_config_t empty = {0};
    const esp_err_t disconnect_result = esp_wifi_disconnect();
    if (disconnect_result != ESP_OK && disconnect_result != ESP_ERR_WIFI_NOT_CONNECT) {
        return disconnect_result;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &empty), TAG, "clear config");
    portENTER_CRITICAL(&s_lock);
    s_status.ssid[0] = '\0';
    s_status.ip[0] = '\0';
    s_status.state = WIFI_MANAGER_IDLE;
    s_status.generation++;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t wifi_manager_set_low_power(bool enabled)
{
    return esp_wifi_set_ps(enabled ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
}

void wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

size_t wifi_manager_get_networks(wifi_manager_network_t *networks, size_t capacity)
{
    if (networks == NULL || capacity == 0) {
        return 0;
    }
    portENTER_CRITICAL(&s_lock);
    size_t count = s_network_count < capacity ? s_network_count : capacity;
    memcpy(networks, s_networks, count * sizeof(networks[0]));
    portEXIT_CRITICAL(&s_lock);
    return count;
}
