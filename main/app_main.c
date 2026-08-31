#include "alarm_audio.h"
#include "alarm_store.h"
#include "board_display.h"
#include "rtc_pcf85063.h"
#include "study_store.h"
#include "study_ui.h"
#include "wifi_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "homework_timer";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(rtc_pcf85063_init());
    rtc_datetime_t now = {0};
    if (!rtc_pcf85063_read(&now)) {
        ESP_ERROR_CHECK(rtc_pcf85063_set_build_time());
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_ERROR_CHECK(rtc_pcf85063_read(&now) ? ESP_OK : ESP_FAIL);
    }
    ESP_ERROR_CHECK(study_store_init(&now));
    ESP_ERROR_CHECK(alarm_store_init());
    ESP_ERROR_CHECK(wifi_manager_init());

    err = alarm_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm audio unavailable: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(board_display_init());
    if (board_display_lock(-1)) {
        ESP_ERROR_CHECK(study_ui_start());
        board_display_unlock();
    }

    ESP_LOGI(TAG, "HomeworkTimer ready");
    vTaskDelete(NULL);
}
