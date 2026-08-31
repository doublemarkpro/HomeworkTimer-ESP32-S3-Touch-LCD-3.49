#include "alarm_store.h"

#include <string.h>

#include "esp_check.h"
#include "nvs.h"
#include "study_store.h"

#define ALARM_MAGIC   0x414C524DU
#define ALARM_VERSION 1U
#define ALARM_NS      "alarm_clock"
#define ALARM_KEY     "config"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    alarm_config_t config;
} alarm_blob_t;

static const char *TAG = "alarm_store";
static nvs_handle_t s_nvs;
static alarm_blob_t s_alarm;

static esp_err_t write_alarm(void)
{
    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs, ALARM_KEY, &s_alarm, sizeof(s_alarm)), TAG,
                        "write alarm");
    return nvs_commit(s_nvs);
}

esp_err_t alarm_store_init(void)
{
    ESP_RETURN_ON_ERROR(nvs_open(ALARM_NS, NVS_READWRITE, &s_nvs), TAG, "open alarm NVS");
    size_t size = sizeof(s_alarm);
    const esp_err_t result = nvs_get_blob(s_nvs, ALARM_KEY, &s_alarm, &size);
    const bool valid = result == ESP_OK && size == sizeof(s_alarm) &&
                       s_alarm.magic == ALARM_MAGIC && s_alarm.version == ALARM_VERSION &&
                       s_alarm.size == sizeof(s_alarm) && s_alarm.config.hour < 24 &&
                       s_alarm.config.minute < 60;
    if (!valid) {
        memset(&s_alarm, 0, sizeof(s_alarm));
        s_alarm.magic = ALARM_MAGIC;
        s_alarm.version = ALARM_VERSION;
        s_alarm.size = sizeof(s_alarm);
        s_alarm.config.hour = 7;
        s_alarm.config.minute = 0;
        s_alarm.config.days_mask = 0x1F;
        s_alarm.config.enabled = false;
        return write_alarm();
    }
    return ESP_OK;
}

const alarm_config_t *alarm_store_get(void)
{
    return &s_alarm.config;
}

esp_err_t alarm_store_save(const alarm_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL && config->hour < 24 && config->minute < 60,
                        ESP_ERR_INVALID_ARG, TAG, "invalid alarm");
    s_alarm.config = *config;
    return write_alarm();
}

bool alarm_store_matches(const rtc_datetime_t *now)
{
    if (now == NULL || !s_alarm.config.enabled || s_alarm.config.days_mask == 0 ||
        now->hour != s_alarm.config.hour || now->minute != s_alarm.config.minute) {
        return false;
    }
    const int32_t day = study_days_from_civil(now->year, now->month, now->day);
    const uint8_t weekday = study_monday_weekday(day);
    return (s_alarm.config.days_mask & (1U << weekday)) != 0;
}

