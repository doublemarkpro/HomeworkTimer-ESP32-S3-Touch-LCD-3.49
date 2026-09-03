#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t app_settings_init(void);
bool app_settings_button_sound_enabled(void);
esp_err_t app_settings_set_button_sound(bool enabled);
uint8_t app_settings_volume(void);
esp_err_t app_settings_set_volume(uint8_t volume);
uint8_t app_settings_brightness(void);
esp_err_t app_settings_set_brightness(uint8_t brightness);
