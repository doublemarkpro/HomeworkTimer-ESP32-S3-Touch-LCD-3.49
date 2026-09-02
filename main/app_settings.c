#include "app_settings.h"

#include "esp_check.h"
#include "nvs.h"

#define SETTINGS_NS        "app_settings"
#define BUTTON_SOUND_KEY   "button_sound"
#define VOLUME_KEY         "volume"

static const char *TAG = "app_settings";
static nvs_handle_t s_nvs;
static bool s_button_sound = true;
static uint8_t s_volume = 100;

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
    return ESP_OK;
}

bool app_settings_button_sound_enabled(void)
{
    return s_button_sound;
}

esp_err_t app_settings_set_button_sound(bool enabled)
{
    s_button_sound = enabled;
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs, BUTTON_SOUND_KEY, enabled ? 1 : 0), TAG,
                        "write button sound");
    return nvs_commit(s_nvs);
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
