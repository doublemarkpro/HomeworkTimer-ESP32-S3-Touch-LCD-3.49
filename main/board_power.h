#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t board_power_init(void);
esp_err_t board_power_prepare_display(void);
esp_err_t board_power_reset_display(void);
esp_err_t board_power_set_speaker(bool enabled);
