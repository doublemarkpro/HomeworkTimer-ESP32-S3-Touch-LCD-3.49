#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    WEATHER_CONFIG_REQUIRED = 0,
    WEATHER_WAITING_FOR_WIFI,
    WEATHER_LOADING,
    WEATHER_READY,
    WEATHER_FAILED,
} weather_state_t;

#define WEATHER_FORECAST_DAYS 3

typedef struct {
    uint8_t month;
    uint8_t day;
    int16_t temperature_min_c;
    int16_t temperature_max_c;
    uint16_t icon_code;
} weather_forecast_day_t;

typedef struct {
    weather_state_t state;
    weather_forecast_day_t days[WEATHER_FORECAST_DAYS];
    uint8_t day_count;
    uint32_t generation;
} weather_status_t;

esp_err_t weather_manager_init(void);
void weather_manager_refresh(void);
void weather_manager_get_status(weather_status_t *status);
