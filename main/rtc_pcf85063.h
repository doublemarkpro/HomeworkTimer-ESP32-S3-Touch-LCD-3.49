#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_datetime_t;

esp_err_t rtc_pcf85063_init(void);
bool rtc_pcf85063_read(rtc_datetime_t *time);
esp_err_t rtc_pcf85063_set(const rtc_datetime_t *time);
esp_err_t rtc_pcf85063_set_build_time(void);
