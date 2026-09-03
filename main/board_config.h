#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define BOARD_LCD_HOST              SPI3_HOST
#define BOARD_LCD_WIDTH             172
#define BOARD_LCD_HEIGHT            640
#define BOARD_SCREEN_WIDTH          640
#define BOARD_SCREEN_HEIGHT         172

#define BOARD_LCD_PIN_CS            GPIO_NUM_9
#define BOARD_LCD_PIN_PCLK          GPIO_NUM_10
#define BOARD_LCD_PIN_DATA0         GPIO_NUM_11
#define BOARD_LCD_PIN_DATA1         GPIO_NUM_12
#define BOARD_LCD_PIN_DATA2         GPIO_NUM_13
#define BOARD_LCD_PIN_DATA3         GPIO_NUM_14
/* Rev1.1 / V2 hardware (shipping since 2026-06-08).  V2 moved the
 * backlight PWM from GPIO8 to GPIO42 and moved LCD reset to TCA9554 IO5.
 * GPIO21 is LCD_TE and GPIO8 is the expander interrupt on this revision. */
#define BOARD_LCD_PIN_TE            GPIO_NUM_21
#define BOARD_LCD_PIN_BACKLIGHT     GPIO_NUM_42

#define BOARD_TOUCH_I2C_PORT        I2C_NUM_1
#define BOARD_TOUCH_PIN_SCL         GPIO_NUM_18
#define BOARD_TOUCH_PIN_SDA         GPIO_NUM_17
#define BOARD_TOUCH_I2C_ADDRESS     0x3B

#define BOARD_RTC_I2C_PORT          I2C_NUM_0
#define BOARD_RTC_PIN_SCL           GPIO_NUM_48
#define BOARD_RTC_PIN_SDA           GPIO_NUM_47
#define BOARD_RTC_I2C_ADDRESS       0x51

#define BOARD_AUDIO_PIN_MCLK        GPIO_NUM_7
#define BOARD_AUDIO_PIN_BCLK        GPIO_NUM_15
#define BOARD_AUDIO_PIN_WS          GPIO_NUM_46
#define BOARD_AUDIO_PIN_DIN         GPIO_NUM_6
#define BOARD_AUDIO_PIN_DOUT        GPIO_NUM_45
#define BOARD_IO_EXPANDER_ADDR      0x20
#define BOARD_DISPLAY_ENABLE_MASK   (1U << 1)
#define BOARD_DISPLAY_RESET_MASK    (1U << 5)
#define BOARD_POWER_HOLD_PIN_MASK   (1U << 6)
#define BOARD_AUDIO_AMP_PIN_MASK    (1U << 7)
#define BOARD_POWER_BUTTON_PIN      GPIO_NUM_16
#define BOARD_POWER_LONG_PRESS_MS   1200

#define BOARD_LVGL_TICK_MS          5
#define BOARD_LVGL_TASK_MIN_MS      5
#define BOARD_LVGL_TASK_MAX_MS      500
#define BOARD_LVGL_TASK_STACK       (8 * 1024)
#define BOARD_LVGL_TASK_PRIORITY    2

#define BOARD_DMA_LINES             64
#define BOARD_DMA_BUFFER_BYTES      (BOARD_LCD_WIDTH * BOARD_DMA_LINES * 2)
#define BOARD_FRAME_BUFFER_BYTES    (BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * 2)
