idf_component_register(
    SRCS
        "app_main.c"
        "alarm_audio.c"
        "alarm_store.c"
        "board_display.c"
        "rtc_pcf85063.c"
        "study_store.c"
        "study_ui.c"
        "ui_font_16.c"
        "wifi_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_ledc
        esp_driver_spi
        esp_lcd
        esp_timer
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
        lvgl
        esp_lcd_axs15231b
        esp_codec_dev
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
