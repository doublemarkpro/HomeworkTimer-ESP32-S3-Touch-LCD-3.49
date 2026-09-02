#include "alarm_audio.h"

#include <stdint.h>
#include <string.h>

#include "board_config.h"
#include "driver/i2c_master.h"
#include "driver/i2s_tdm.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ALARM_SAMPLE_RATE 16000

static const char *TAG = "alarm_audio";
static i2s_chan_handle_t s_tx;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if;
static esp_codec_dev_handle_t s_playback;
static SemaphoreHandle_t s_audio_mutex;
static volatile bool s_playing;
static volatile bool s_click_playing;
static bool s_available;

static void alarm_tone_task(void *argument)
{
    (void)argument;
    int16_t samples[256 * 2];
    uint32_t phase = 0;
    uint32_t block = 0;
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    while (s_playing) {
        const bool silent = (block % 8U) >= 6U;
        const uint32_t frequency = (block % 16U) < 8U ? 880U : 660U;
        const uint32_t half_period = ALARM_SAMPLE_RATE / (frequency * 2U);
        for (size_t index = 0; index < 256; ++index) {
            int16_t value = 0;
            if (!silent) {
                value = ((phase / half_period) & 1U) != 0 ? 7200 : -7200;
                phase++;
            }
            samples[index * 2] = value;
            samples[index * 2 + 1] = value;
        }
        if (esp_codec_dev_write(s_playback, samples, sizeof(samples)) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "audio write failed");
            break;
        }
        block++;
    }
    memset(samples, 0, sizeof(samples));
    esp_codec_dev_write(s_playback, samples, sizeof(samples));
    xSemaphoreGive(s_audio_mutex);
    s_playing = false;
    vTaskDelete(NULL);
}

static void click_tone_task(void *argument)
{
    (void)argument;
    enum { CLICK_SAMPLES = 320 };
    int16_t samples[CLICK_SAMPLES * 2];
    const uint32_t half_period = ALARM_SAMPLE_RATE / (1500U * 2U);
    for (size_t index = 0; index < CLICK_SAMPLES; ++index) {
        const int32_t envelope = 12000 * (CLICK_SAMPLES - (int32_t)index) / CLICK_SAMPLES;
        const int16_t value = ((index / half_period) & 1U) != 0 ? (int16_t)envelope
                                                                : (int16_t)-envelope;
        samples[index * 2] = value;
        samples[index * 2 + 1] = value;
    }

    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    if (!s_playing) {
        esp_codec_dev_write(s_playback, samples, sizeof(samples));
    }
    xSemaphoreGive(s_audio_mutex);
    s_click_playing = false;
    vTaskDelete(NULL);
}

esp_err_t alarm_audio_init(void)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(BOARD_RTC_I2C_PORT, &i2c_bus), TAG,
                        "get codec I2C bus");

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_tx, NULL), TAG, "I2S channel");

    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 |
                                    I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(ALARM_SAMPLE_RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO, slot_mask),
        .gpio_cfg = {
            .mclk = BOARD_AUDIO_PIN_MCLK,
            .bclk = BOARD_AUDIO_PIN_BCLK,
            .ws = BOARD_AUDIO_PIN_WS,
            .dout = BOARD_AUDIO_PIN_DOUT,
            .din = BOARD_AUDIO_PIN_DIN,
        },
    };
    tdm_config.slot_cfg.total_slot = 4;
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_tx, &tdm_config), TAG, "I2S TDM");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable I2S");

    audio_codec_i2s_cfg_t data_config = {
        .port = I2S_NUM_0,
        .tx_handle = s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&data_config);
    audio_codec_i2c_cfg_t control_config = {
        .port = BOARD_RTC_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&control_config);
    s_gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t codec_config = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .pa_pin = GPIO_NUM_NC,
        .use_mclk = true,
        .hw_gain.pa_gain = 6,
    };
    s_codec_if = es8311_codec_new(&codec_config);
    esp_codec_dev_cfg_t device_config = {
        .codec_if = s_codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_playback = esp_codec_dev_new(&device_config);
    if (s_data_if == NULL || s_ctrl_if == NULL || s_gpio_if == NULL || s_codec_if == NULL ||
        s_playback == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = ALARM_SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_playback, &sample_info) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    s_audio_mutex = xSemaphoreCreateMutex();
    if (s_audio_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_available = true;
    ESP_LOGI(TAG, "ES8311 alarm audio ready");
    return ESP_OK;
}

esp_err_t alarm_audio_set_volume(uint8_t volume)
{
    if (!s_available || volume > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_codec_dev_set_out_vol(s_playback, (float)volume) == ESP_CODEC_DEV_OK
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t alarm_audio_click(void)
{
    if (!s_available || s_playing) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_click_playing) {
        return ESP_OK;
    }
    s_click_playing = true;
    if (xTaskCreate(click_tone_task, "button_click", 3072, NULL, 3, NULL) != pdPASS) {
        s_click_playing = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t alarm_audio_start(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        return ESP_OK;
    }
    s_playing = true;
    if (xTaskCreate(alarm_tone_task, "alarm_tone", 3072, NULL, 4, NULL) != pdPASS) {
        s_playing = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void alarm_audio_stop(void)
{
    s_playing = false;
}

bool alarm_audio_available(void)
{
    return s_available;
}
