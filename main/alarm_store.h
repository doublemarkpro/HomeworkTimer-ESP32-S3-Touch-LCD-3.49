#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rtc_pcf85063.h"

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t days_mask;
    bool enabled;
} alarm_config_t;

esp_err_t alarm_store_init(void);
const alarm_config_t *alarm_store_get(void);
esp_err_t alarm_store_save(const alarm_config_t *config);
bool alarm_store_matches(const rtc_datetime_t *now);

