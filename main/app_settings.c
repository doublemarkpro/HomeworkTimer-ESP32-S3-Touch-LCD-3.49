#include "app_settings.h"

#include "esp_check.h"
#include "nvs.h"

#define SETTINGS_NS        "app_settings"
#define BUTTON_SOUND_KEY   "button_sound"
#define VOLUME_KEY         "volume"
#define BRIGHTNESS_KEY     "brightness"
#define AUTO_LOCK_KEY      "auto_lock"
#define FOCUS_CHINESE_KEY  "focus_cn"
#define FOCUS_MATH_KEY     "focus_math"
#define FOCUS_ENGLISH_KEY  "focus_en"

static const char *TAG = "app_settings";
static nvs_handle_t s_nvs;
static bool s_button_sound = true;
static uint8_t s_volume = 100;
static uint8_t s_brightness = 100;
static uint8_t s_auto_lock_minutes = 5;
static uint8_t s_focus_minutes[3] = {45, 45, 45};

static const char *const s_focus_keys[] = {
    FOCUS_CHINESE_KEY, FOCUS_MATH_KEY, FOCUS_ENGLISH_KEY,
};

static bool valid_auto_lock_minutes(uint8_t minutes)
{
    return minutes == 0 || minutes == 1 || minutes == 5 || minutes == 10 ||
           minutes == 30;
}

esp_err_t app_settings_init(void)
{
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NS, NVS_READWRITE, &s_nvs), TAG,
                        "open settings NVS");

    uint8_t stored = 1;
    const esp_err_t result = nvs_get_u8(s_nvs, BUTTON_SOUND_KEY, &stored);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, BUTTON_SOUND_KEY, 1), TAG,
                            "write default button sound");
        ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG, "commit default settings");
    } else {
        ESP_RETURN_ON_ERROR(result, TAG, "read button sound");
    }
    s_button_sound = stored != 0;

    stored = 100;
    const esp_err_t volume_result = nvs_get_u8(s_nvs, VOLUME_KEY, &stored);
    if (volume_result == ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, VOLUME_KEY, 100), TAG,
                            "write default volume");
        ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG, "commit default volume");
    } else {
        ESP_RETURN_ON_ERROR(volume_result, TAG, "read volume");
    }
    s_volume = stored <= 100 ? stored : 100;

    stored = 100;
    const esp_err_t brightness_result = nvs_get_u8(s_nvs, BRIGHTNESS_KEY, &stored);
    if (brightness_result == ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, BRIGHTNESS_KEY, 100), TAG,
                            "write default brightness");
        ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG, "commit default brightness");
    } else {
        ESP_RETURN_ON_ERROR(brightness_result, TAG, "read brightness");
    }
    s_brightness = stored >= 10 && stored <= 100 ? stored : 100;

    stored = 5;
    const esp_err_t auto_lock_result = nvs_get_u8(s_nvs, AUTO_LOCK_KEY, &stored);
    if (auto_lock_result == ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, AUTO_LOCK_KEY, 5), TAG,
                            "write default auto lock");
        ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG, "commit default auto lock");
    } else {
        ESP_RETURN_ON_ERROR(auto_lock_result, TAG, "read auto lock");
    }
    s_auto_lock_minutes = valid_auto_lock_minutes(stored) ? stored : 5;

    for (uint8_t subject = 0; subject < 3; ++subject) {
        stored = 45;
        const esp_err_t focus_result = nvs_get_u8(s_nvs, s_focus_keys[subject], &stored);
        if (focus_result == ESP_ERR_NVS_NOT_FOUND) {
            ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, s_focus_keys[subject], 45), TAG,
                                "write default focus duration");
            ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG,
                                "commit default focus duration");
        } else {
            ESP_RETURN_ON_ERROR(focus_result, TAG, "read focus duration");
        }
        s_focus_minutes[subject] = stored >= 10 && stored <= 90 ? stored : 45;
    }
    return ESP_OK;
}

bool app_settings_button_sound_enabled(void)
{
    return s_button_sound;
}

esp_err_t app_settings_set_button_sound(bool enabled)
{
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, BUTTON_SOUND_KEY, enabled ? 1 : 0), TAG,
                        "write button sound");
    ESP_RETURN_ON_ERROR(nvs_commit(s_nvs), TAG, "commit button sound");
    s_button_sound = enabled;
    return ESP_OK;
}

uint8_t app_settings_volume(void)
{
    return s_volume;
}

esp_err_t app_settings_set_volume(uint8_t volume)
{
    ESP_RETURN_ON_FALSE(volume <= 100, ESP_ERR_INVALID_ARG, TAG, "invalid volume");
    s_volume = volume;
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, VOLUME_KEY, volume), TAG, "write volume");
    return nvs_commit(s_nvs);
}

uint8_t app_settings_brightness(void)
{
    return s_brightness;
}

esp_err_t app_settings_set_brightness(uint8_t brightness)
{
    ESP_RETURN_ON_FALSE(brightness >= 10 && brightness <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "invalid brightness");
    s_brightness = brightness;
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, BRIGHTNESS_KEY, brightness), TAG,
                        "write brightness");
    return nvs_commit(s_nvs);
}

uint8_t app_settings_auto_lock_minutes(void)
{
    return s_auto_lock_minutes;
}

esp_err_t app_settings_set_auto_lock_minutes(uint8_t minutes)
{
    ESP_RETURN_ON_FALSE(valid_auto_lock_minutes(minutes), ESP_ERR_INVALID_ARG, TAG,
                        "invalid auto lock timeout");
    s_auto_lock_minutes = minutes;
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, AUTO_LOCK_KEY, minutes), TAG,
                        "write auto lock timeout");
    return nvs_commit(s_nvs);
}

uint8_t app_settings_focus_minutes(uint8_t subject)
{
    return subject < 3 ? s_focus_minutes[subject] : 45;
}

esp_err_t app_settings_set_focus_minutes(uint8_t subject, uint8_t minutes)
{
    ESP_RETURN_ON_FALSE(subject < 3, ESP_ERR_INVALID_ARG, TAG, "invalid subject");
    ESP_RETURN_ON_FALSE(minutes >= 10 && minutes <= 90, ESP_ERR_INVALID_ARG, TAG,
                        "invalid focus duration");
    s_focus_minutes[subject] = minutes;
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, s_focus_keys[subject], minutes), TAG,
                        "write focus duration");
    return nvs_commit(s_nvs);
}
