#include "time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "rtc_pcf85063.h"

static const char *TAG = "time_sync";

static void time_sync_callback(struct timeval *time_value)
{
    if (time_value == NULL) {
        return;
    }
    struct tm local = {0};
    const time_t seconds = time_value->tv_sec;
    if (localtime_r(&seconds, &local) == NULL || local.tm_year + 1900 < 2024) {
        ESP_LOGW(TAG, "ignoring invalid network time");
        return;
    }
    const rtc_datetime_t rtc_time = {
        .year = (uint16_t)(local.tm_year + 1900),
        .month = (uint8_t)(local.tm_mon + 1),
        .day = (uint8_t)local.tm_mday,
        .weekday = (uint8_t)local.tm_wday,
        .hour = (uint8_t)local.tm_hour,
        .minute = (uint8_t)local.tm_min,
        .second = (uint8_t)local.tm_sec,
    };
    const esp_err_t error = rtc_pcf85063_set(&rtc_time);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "RTC synchronized: %04u-%02u-%02u %02u:%02u:%02u",
                 rtc_time.year, rtc_time.month, rtc_time.day, rtc_time.hour,
                 rtc_time.minute, rtc_time.second);
    } else {
        ESP_LOGE(TAG, "RTC synchronization failed: %s", esp_err_to_name(error));
    }
}

esp_err_t time_sync_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    config.sync_cb = time_sync_callback;
    const esp_err_t error = esp_netif_sntp_init(&config);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "SNTP started (UTC+8)");
    }
    return error;
}
