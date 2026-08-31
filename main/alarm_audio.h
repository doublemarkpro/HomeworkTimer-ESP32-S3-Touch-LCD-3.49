#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t alarm_audio_init(void);
esp_err_t alarm_audio_start(void);
void alarm_audio_stop(void);
bool alarm_audio_available(void);

