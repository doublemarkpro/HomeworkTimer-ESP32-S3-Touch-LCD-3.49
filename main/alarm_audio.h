#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t alarm_audio_init(void);
esp_err_t alarm_audio_start(void);
esp_err_t alarm_audio_click(void);
esp_err_t alarm_audio_set_volume(uint8_t volume);
void alarm_audio_stop(void);
bool alarm_audio_available(void);
