#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_display_init(void);
bool board_display_lock(int timeout_ms);
void board_display_unlock(void);
void board_display_set_backlight(uint8_t percent);
esp_err_t board_display_set_sleeping(bool sleeping);
void board_display_block_touch_until_release(void);

#ifdef __cplusplus
}
#endif
