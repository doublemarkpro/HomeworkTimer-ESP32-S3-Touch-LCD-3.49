#include "board_power.h"

#include <stdint.h>

#include "board_config.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TCA9554_OUTPUT_REG 0x01
#define TCA9554_CONFIG_REG 0x03

static const char *TAG = "board_power";
static i2c_master_dev_handle_t s_expander;
static esp_err_t set_output(uint8_t mask, bool high);

static void power_button_task(void *argument)
{
    (void)argument;
    bool armed = false;
    int64_t pressed_since_us = 0;

    for (;;) {
        const bool pressed = gpio_get_level(BOARD_POWER_BUTTON_PIN) == 0;

        /* Starting from battery requires holding this same key. Do not arm
         * shutdown until that initial press has been released once. */
        if (!armed) {
            if (!pressed) {
                armed = true;
                ESP_LOGI(TAG, "power button armed");
            }
        } else if (!pressed) {
            pressed_since_us = 0;
        } else if (pressed_since_us == 0) {
            pressed_since_us = esp_timer_get_time();
        } else if (esp_timer_get_time() - pressed_since_us >=
                   (int64_t)BOARD_POWER_LONG_PRESS_MS * 1000) {
            ESP_LOGI(TAG, "power button long press: releasing battery latch");
            /* Give immediate visual/audio feedback. Battery power is finally
             * removed when the user releases the physical key. */
            (void)set_output(BOARD_DISPLAY_ENABLE_MASK, false);
            (void)set_output(BOARD_AUDIO_AMP_PIN_MASK, false);
            (void)set_output(BOARD_POWER_HOLD_PIN_MASK, false);
            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_expander, &reg, 1, value, 1, 100);
}

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t command[] = {reg, value};
    return i2c_master_transmit(s_expander, command, sizeof(command), 100);
}

static esp_err_t set_output(uint8_t mask, bool high)
{
    uint8_t output = 0;
    uint8_t config = 0;
    ESP_RETURN_ON_ERROR(read_register(TCA9554_OUTPUT_REG, &output), TAG,
                        "read TCA9554 output");
    ESP_RETURN_ON_ERROR(read_register(TCA9554_CONFIG_REG, &config), TAG,
                        "read TCA9554 config");
    output = high ? (uint8_t)(output | mask) : (uint8_t)(output & ~mask);
    config = (uint8_t)(config & ~mask);
    ESP_RETURN_ON_ERROR(write_register(TCA9554_OUTPUT_REG, output), TAG,
                        "set TCA9554 output");
    return write_register(TCA9554_CONFIG_REG, config);
}

esp_err_t board_power_init(void)
{
    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(BOARD_RTC_I2C_PORT, &bus), TAG,
                        "get board I2C bus");
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_IO_EXPANDER_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device_config, &s_expander), TAG,
                        "add TCA9554");
    ESP_RETURN_ON_ERROR(set_output(BOARD_POWER_HOLD_PIN_MASK, true), TAG,
                        "latch battery power");

    const gpio_config_t power_button_config = {
        .pin_bit_mask = 1ULL << BOARD_POWER_BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&power_button_config), TAG,
                        "configure power button");
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(power_button_task, "power_button", 3072,
                                                NULL, 3, NULL, 1) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create power button task");
    ESP_LOGI(TAG, "battery power latched through TCA9554 IO6");
    return ESP_OK;
}

esp_err_t board_power_prepare_display(void)
{
    ESP_RETURN_ON_FALSE(s_expander != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "power expander unavailable");
    ESP_RETURN_ON_ERROR(set_output(BOARD_DISPLAY_ENABLE_MASK, true), TAG,
                        "enable V2 LCD backlight circuit");
    ESP_RETURN_ON_ERROR(set_output(BOARD_DISPLAY_RESET_MASK, true), TAG,
                        "release V2 LCD reset");
    ESP_LOGI(TAG, "V2 display enabled through TCA9554 IO1");
    return ESP_OK;
}

esp_err_t board_power_reset_display(void)
{
    ESP_RETURN_ON_FALSE(s_expander != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "power expander unavailable");
    ESP_RETURN_ON_ERROR(set_output(BOARD_DISPLAY_RESET_MASK, true), TAG,
                        "release display reset");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(set_output(BOARD_DISPLAY_RESET_MASK, false), TAG,
                        "assert display reset");
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_RETURN_ON_ERROR(set_output(BOARD_DISPLAY_RESET_MASK, true), TAG,
                        "release display reset");
    vTaskDelay(pdMS_TO_TICKS(30));
    return ESP_OK;
}

esp_err_t board_power_set_speaker(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_expander != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "power expander unavailable");
    ESP_RETURN_ON_ERROR(set_output(BOARD_AUDIO_AMP_PIN_MASK, enabled), TAG,
                        "set speaker amplifier");
    ESP_LOGI(TAG, "speaker amplifier %s through TCA9554 IO7",
             enabled ? "enabled" : "disabled");
    return ESP_OK;
}
