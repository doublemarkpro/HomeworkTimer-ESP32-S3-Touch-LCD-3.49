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
uint8_t app_settings_auto_lock_minutes(void);
esp_err_t app_settings_set_auto_lock_minutes(uint8_t minutes);
uint8_t app_settings_focus_minutes(uint8_t subject);
esp_err_t app_settings_set_focus_minutes(uint8_t subject, uint8_t minutes);
