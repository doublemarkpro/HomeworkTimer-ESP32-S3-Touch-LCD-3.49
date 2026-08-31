#include "rtc_pcf85063.h"

#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "rtc";
static i2c_master_bus_handle_t s_rtc_bus;
static i2c_master_dev_handle_t s_rtc_device;

static uint8_t bcd_to_dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

static uint8_t dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static int32_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const int adjusted_month = (int)month + (month > 2 ? -3 : 9);
    const unsigned day_of_year = (153U * (unsigned)adjusted_month + 2U) / 5U + day - 1U;
    const unsigned day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return (int32_t)(era * 146097 + (int)day_of_era - 719468);
}

static uint8_t sunday_weekday(int32_t civil_day)
{
    int weekday = (civil_day + 4) % 7;
    return (uint8_t)(weekday < 0 ? weekday + 7 : weekday);
}

esp_err_t rtc_pcf85063_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_RTC_I2C_PORT,
        .sda_io_num = BOARD_RTC_PIN_SDA,
        .scl_io_num = BOARD_RTC_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_rtc_bus), TAG, "RTC I2C bus");

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_RTC_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(s_rtc_bus, &device_config, &s_rtc_device);
}

bool rtc_pcf85063_read(rtc_datetime_t *time)
{
    if (time == NULL || s_rtc_device == NULL) {
        return false;
    }
    const uint8_t register_address = 0x04;
    uint8_t data[7] = {0};
    if (i2c_master_transmit_receive(s_rtc_device, &register_address, 1, data, sizeof(data), 100) !=
        ESP_OK) {
        return false;
    }
    if ((data[0] & 0x80) != 0) {
        return false;
    }

    time->second = bcd_to_dec(data[0] & 0x7F);
    time->minute = bcd_to_dec(data[1] & 0x7F);
    time->hour = bcd_to_dec(data[2] & 0x3F);
    time->day = bcd_to_dec(data[3] & 0x3F);
    time->weekday = data[4] & 0x07;
    time->month = bcd_to_dec(data[5] & 0x1F);
    time->year = (uint16_t)(2000 + bcd_to_dec(data[6]));

    return time->year >= 2024 && time->year <= 2099 && time->month >= 1 && time->month <= 12 &&
           time->day >= 1 && time->day <= 31 && time->hour < 24 && time->minute < 60 &&
           time->second < 60;
}

esp_err_t rtc_pcf85063_set(const rtc_datetime_t *time)
{
    ESP_RETURN_ON_FALSE(time != NULL && s_rtc_device != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "RTC not ready");
    uint8_t packet[8] = {
        0x04,
        dec_to_bcd(time->second),
        dec_to_bcd(time->minute),
        dec_to_bcd(time->hour),
        dec_to_bcd(time->day),
        time->weekday,
        dec_to_bcd(time->month),
        dec_to_bcd((uint8_t)(time->year % 100)),
    };
    return i2c_master_transmit(s_rtc_device, packet, sizeof(packet), 100);
}

static uint8_t build_month(const char *date)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    for (uint8_t index = 0; index < 12; ++index) {
        if (strncmp(date, months + index * 3, 3) == 0) {
            return (uint8_t)(index + 1);
        }
    }
    return 1;
}

esp_err_t rtc_pcf85063_set_build_time(void)
{
    unsigned day = 1;
    unsigned year = 2026;
    unsigned hour = 8;
    unsigned minute = 0;
    unsigned second = 0;
    if (sscanf(__DATE__ + 4, "%u %u", &day, &year) != 2 ||
        sscanf(__TIME__, "%u:%u:%u", &hour, &minute, &second) != 3) {
        return ESP_ERR_INVALID_ARG;
    }

    rtc_datetime_t time = {
        .year = (uint16_t)year,
        .month = build_month(__DATE__),
        .day = (uint8_t)day,
        .hour = (uint8_t)hour,
        .minute = (uint8_t)minute,
        .second = (uint8_t)second,
    };
    time.weekday = sunday_weekday(days_from_civil(time.year, time.month, time.day));
    ESP_LOGW(TAG, "RTC invalid; initializing from firmware build time");
    return rtc_pcf85063_set(&time);
}
