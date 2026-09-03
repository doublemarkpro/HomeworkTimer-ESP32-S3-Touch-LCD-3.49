#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t battery_monitor_init(void);
bool battery_monitor_read(uint16_t *millivolts, uint8_t *percent);
bool battery_monitor_usb_powered(void);
