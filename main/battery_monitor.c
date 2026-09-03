#include "battery_monitor.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "soc/usb_serial_jtag_struct.h"

#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_DIVIDER_RATIO 3
#define BATTERY_SAMPLE_COUNT 8

static const char *TAG = "battery_monitor";
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;
static bool s_calibrated;

bool battery_monitor_usb_powered(void)
{
    /* VBUS/charger STAT is not routed to an ESP32 GPIO on this board. The
     * built-in USB Serial/JTAG peripheral does receive USB SOF frames while a
     * host is attached, so track its frame counter to distinguish the COM/USB
     * powered state from battery-only operation. */
    static bool initialized;
    static uint16_t last_frame;
    static int64_t last_usb_activity_us;
    const uint16_t frame = (uint16_t)USB_SERIAL_JTAG.fram_num.sof_frame_index;
    const int64_t now_us = esp_timer_get_time();

    if (!initialized) {
        initialized = true;
        last_frame = frame;
        if (frame != 0) {
            last_usb_activity_us = now_us;
        }
    } else if (frame != last_frame) {
        last_frame = frame;
        last_usb_activity_us = now_us;
    }

    return last_usb_activity_us != 0 &&
           now_us - last_usb_activity_us < 2500000;
}

static uint8_t voltage_to_percent(uint16_t millivolts)
{
    /* A deliberately conservative one-cell Li-ion curve. The display is
     * intended as a useful status indication, not a laboratory fuel gauge. */
    static const struct {
        uint16_t millivolts;
        uint8_t percent;
    } curve[] = {
        {3300, 0}, {3500, 10}, {3650, 25}, {3750, 45},
        {3850, 65}, {3950, 80}, {4050, 92}, {4200, 100},
    };

    if (millivolts <= curve[0].millivolts) {
        return 0;
    }
    for (size_t index = 1; index < sizeof(curve) / sizeof(curve[0]); ++index) {
        if (millivolts <= curve[index].millivolts) {
            const uint16_t voltage_span = curve[index].millivolts - curve[index - 1].millivolts;
            const uint16_t voltage_offset = millivolts - curve[index - 1].millivolts;
            const uint8_t percent_span = curve[index].percent - curve[index - 1].percent;
            return curve[index - 1].percent +
                   (uint8_t)((uint32_t)voltage_offset * percent_span / voltage_span);
        }
    }
    return 100;
}

esp_err_t battery_monitor_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &s_adc_handle), TAG,
                        "create ADC unit");

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL,
                                                   &channel_config),
                        TAG, "configure battery ADC");

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    const esp_err_t calibration_error =
        adc_cali_create_scheme_curve_fitting(&calibration_config, &s_cali_handle);
    s_calibrated = calibration_error == ESP_OK;
    if (!s_calibrated) {
        ESP_LOGW(TAG, "ADC calibration unavailable: %s", esp_err_to_name(calibration_error));
    }
#endif

    ESP_LOGI(TAG, "battery ADC ready on ADC1 channel 3%s",
             s_calibrated ? " (calibrated)" : "");
    return ESP_OK;
}

bool battery_monitor_read(uint16_t *millivolts, uint8_t *percent)
{
    if (s_adc_handle == NULL || millivolts == NULL || percent == NULL) {
        return false;
    }

    uint32_t raw_total = 0;
    for (int sample = 0; sample < BATTERY_SAMPLE_COUNT; ++sample) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            return false;
        }
        raw_total += (uint32_t)raw;
    }
    const int raw_average = (int)(raw_total / BATTERY_SAMPLE_COUNT);

    int pin_millivolts = 0;
    if (!s_calibrated ||
        adc_cali_raw_to_voltage(s_cali_handle, raw_average, &pin_millivolts) != ESP_OK) {
        pin_millivolts = (raw_average * 3300) / 4095;
    }
    const uint32_t battery_millivolts = (uint32_t)pin_millivolts * BATTERY_DIVIDER_RATIO;
    *millivolts = battery_millivolts > UINT16_MAX ? UINT16_MAX
                                                  : (uint16_t)battery_millivolts;
    *percent = voltage_to_percent(*millivolts);
    return true;
}
