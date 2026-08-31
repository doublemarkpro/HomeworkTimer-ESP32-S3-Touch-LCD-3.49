#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_MANAGER_MAX_NETWORKS 5

typedef enum {
    WIFI_MANAGER_OFF = 0,
    WIFI_MANAGER_IDLE,
    WIFI_MANAGER_SCANNING,
    WIFI_MANAGER_CONNECTING,
    WIFI_MANAGER_CONNECTED,
    WIFI_MANAGER_FAILED,
} wifi_manager_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
} wifi_manager_network_t;

typedef struct {
    wifi_manager_state_t state;
    char ssid[33];
    char ip[16];
    int8_t rssi;
    uint32_t generation;
} wifi_manager_status_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_scan(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_disconnect(void);
void wifi_manager_get_status(wifi_manager_status_t *status);
size_t wifi_manager_get_networks(wifi_manager_network_t *networks, size_t capacity);

