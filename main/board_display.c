#include "board_display.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "board_config.h"
#include "board_power.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "board_display";

static SemaphoreHandle_t s_lvgl_mutex;
static SemaphoreHandle_t s_flush_done;
static i2c_master_bus_handle_t s_touch_bus;
static i2c_master_dev_handle_t s_touch_device;
static uint16_t *s_dma_buffer;
static uint8_t *s_rotated_buffer;
static int64_t s_last_touch_error_log_us;
static esp_lcd_panel_handle_t s_panel;
static lv_indev_t *s_touch_input;
static uint8_t s_last_backlight_percent = 100;
static bool s_display_sleeping;
static bool s_touch_release_gate;
static int64_t s_touch_release_since_us;
static bool s_touch_pressed;
static int64_t s_touch_missing_since_us;
static lv_point_t s_last_touch_point;

#define TOUCH_RELEASE_STABLE_US 250000
#define TOUCH_GESTURE_RELEASE_STABLE_US 40000
#define TOUCH_I2C_TIMEOUT_MS 15

static const axs15231b_lcd_init_cmd_t s_lcd_init_commands[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

static bool lcd_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *event_data,
                              void *user_context)
{
    (void)panel_io;
    (void)event_data;
    (void)user_context;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &task_woken);
    return task_woken == pdTRUE;
}

static void lvgl_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(display);
    lv_draw_sw_rgb565_swap(pixels, lv_area_get_width(area) * lv_area_get_height(area));

    lv_area_t physical_area = *area;
    const lv_display_rotation_t rotation = lv_display_get_rotation(display);
    if (rotation != LV_DISPLAY_ROTATION_0) {
        const lv_color_format_t format = lv_display_get_color_format(display);
        lv_display_rotate_area(display, &physical_area);
        const uint32_t source_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), format);
        const uint32_t destination_stride =
            lv_draw_buf_width_to_stride(lv_area_get_width(&physical_area), format);
        lv_draw_sw_rotate(pixels, s_rotated_buffer, lv_area_get_width(area), lv_area_get_height(area),
                          source_stride, destination_stride, rotation, format);
        pixels = s_rotated_buffer;
    }

    const int chunk_count = BOARD_FRAME_BUFFER_BYTES / BOARD_DMA_BUFFER_BYTES;
    const int lines_per_chunk = BOARD_LCD_HEIGHT / chunk_count;
    const int pixels_per_chunk = BOARD_DMA_BUFFER_BYTES / 2;
    uint16_t *source = (uint16_t *)pixels;

    xSemaphoreGive(s_flush_done);
    for (int chunk = 0; chunk < chunk_count; ++chunk) {
        xSemaphoreTake(s_flush_done, portMAX_DELAY);
        memcpy(s_dma_buffer, source, BOARD_DMA_BUFFER_BYTES);
        const int y1 = chunk * lines_per_chunk;
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_lcd_panel_draw_bitmap(panel, 0, y1, BOARD_LCD_WIDTH, y1 + lines_per_chunk,
                                      s_dma_buffer));
        source += pixels_per_chunk;
    }
    xSemaphoreTake(s_flush_done, portMAX_DELAY);
    lv_display_flush_ready(display);
}

static void touch_read(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    static const uint8_t command[11] = {
        0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00,
    };
    uint8_t response[32] = {0};
    const esp_err_t err = i2c_master_transmit_receive(
        s_touch_device, command, sizeof(command), response, sizeof(response),
        TOUCH_I2C_TIMEOUT_MS);
    const int64_t now_us = esp_timer_get_time();
    const bool valid_sample = err == ESP_OK;
    const bool raw_pressed = valid_sample && response[1] > 0 && response[1] < 5;

    if (s_touch_release_gate) {
        /* Screen replacement must never inherit the touch that unlocked it.
         * Require a continuous, trustworthy released interval because this
         * controller can briefly report zero points during a long press. */
        if (!valid_sample || raw_pressed) {
            s_touch_release_since_us = 0;
        } else if (s_touch_release_since_us == 0) {
            s_touch_release_since_us = now_us;
        } else if (now_us - s_touch_release_since_us >= TOUCH_RELEASE_STABLE_US) {
            s_touch_release_gate = false;
            s_touch_release_since_us = 0;
            ESP_LOGI(TAG, "touch release gate cleared");
        }
        s_touch_pressed = false;
        s_touch_missing_since_us = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (!raw_pressed) {
        if (err != ESP_OK && now_us - s_last_touch_error_log_us > 1000000) {
            ESP_LOGW(TAG, "touch I2C read failed: %s", esp_err_to_name(err));
            s_last_touch_error_log_us = now_us;
        }

        /* The AXS15231B touch data can briefly contain zero points while a
         * finger is still moving. Keep the last real point for a very short
         * interval so LVGL sees one continuous gesture instead of a release,
         * snap-back, and a second press. A genuine release is delayed by only
         * four 10 ms input samples. */
        if (s_touch_pressed) {
            if (s_touch_missing_since_us == 0) {
                s_touch_missing_since_us = now_us;
            }
            if (now_us - s_touch_missing_since_us < TOUCH_GESTURE_RELEASE_STABLE_US) {
                data->state = LV_INDEV_STATE_PRESSED;
                data->point = s_last_touch_point;
                return;
            }
        }

        s_touch_pressed = false;
        s_touch_missing_since_us = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    s_touch_missing_since_us = 0;

    uint16_t point_x = ((uint16_t)(response[2] & 0x0F) << 8) | response[3];
    uint16_t point_y = ((uint16_t)(response[4] & 0x0F) << 8) | response[5];
    if (point_x >= BOARD_LCD_HEIGHT) {
        point_x = BOARD_LCD_HEIGHT - 1;
    }
    if (point_y >= BOARD_LCD_WIDTH) {
        point_y = BOARD_LCD_WIDTH - 1;
    }

    /* Waveshare's LVGL 9 example supplies the unrotated panel coordinate;
     * LVGL then applies LV_DISPLAY_ROTATION_270 to obtain screen coordinates. */
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = point_y;
    data->point.y = BOARD_LCD_HEIGHT - 1 - point_x;
    s_last_touch_point = data->point;
    s_touch_pressed = true;
}

void board_display_block_touch_until_release(void)
{
    s_touch_release_since_us = 0;
    s_touch_release_gate = true;
    s_touch_pressed = false;
    s_touch_missing_since_us = 0;
    if (s_touch_input != NULL) {
        lv_indev_wait_release(s_touch_input);
    }
    ESP_LOGI(TAG, "touch blocked until stable release");
}

static void lvgl_tick(void *argument)
{
    (void)argument;
    lv_tick_inc(BOARD_LVGL_TICK_MS);
}

bool board_display_lock(int timeout_ms)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }
    const TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mutex, ticks) == pdTRUE;
}

void board_display_unlock(void)
{
    assert(s_lvgl_mutex != NULL);
    xSemaphoreGive(s_lvgl_mutex);
}

static void lvgl_task(void *argument)
{
    (void)argument;
    uint32_t delay_ms = BOARD_LVGL_TASK_MAX_MS;
    while (true) {
        if (board_display_lock(-1)) {
            delay_ms = lv_timer_handler();
            board_display_unlock();
        }
        if (delay_ms > BOARD_LVGL_TASK_MAX_MS) {
            delay_ms = BOARD_LVGL_TASK_MAX_MS;
        } else if (delay_ms < BOARD_LVGL_TASK_MIN_MS) {
            delay_ms = BOARD_LVGL_TASK_MIN_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void board_display_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    if (percent > 0) {
        s_last_backlight_percent = percent;
    }
    /* V2/Rev1.1 routes the PWM to GPIO42 and the AP3032 control is active-low.
     * On the real panel, duty values above roughly 128 already make the image
     * unreadable, so map the UI's 10..100% onto the useful 115..0 range. */
    const uint32_t visible_percent = percent < 10U ? 10U : percent;
    const uint32_t duty = percent == 0 ? 255U
                                       : ((100U - visible_percent) * 115U + 45U) / 90U;
    /* Keep this in the same two calls used by Waveshare. The combined
     * ledc_set_duty_and_update() API depends on the optional fade service and
     * can leave the output unchanged when that service is not installed. */
    esp_err_t error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    if (error == ESP_OK) {
        error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "set backlight to %u%% failed: %s", percent,
                 esp_err_to_name(error));
    } else {
        ESP_LOGI(TAG, "backlight %u%% (PWM duty %" PRIu32 ")", percent, duty);
    }
}

esp_err_t board_display_set_sleeping(bool sleeping)
{
    if (s_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sleeping == s_display_sleeping) {
        return ESP_OK;
    }

    esp_err_t first_error = ESP_OK;
    if (sleeping) {
        if (s_touch_input != NULL) {
            lv_indev_enable(s_touch_input, false);
        }
        board_display_set_backlight(0);
        /* AXS15231B on this 172x640 panel corrupts its scan state after the
         * generic display-off/display-on command pair. Keep the controller
         * initialized and save power through the backlight, touch polling,
         * amplifier and Wi-Fi instead. */
        const esp_err_t speaker_error = board_power_set_speaker(false);
        if (speaker_error != ESP_OK && first_error == ESP_OK) first_error = speaker_error;
    } else {
        /* The audio module owns amplifier enable and turns it on only while a
         * click or alarm is actually playing. Leaving it off here prevents a
         * wake/reset pop and saves idle power. */
        board_display_set_backlight(s_last_backlight_percent);
        if (s_touch_input != NULL) {
            lv_indev_enable(s_touch_input, true);
        }
    }
    s_display_sleeping = sleeping;
    ESP_LOGI(TAG, "display %s", sleeping ? "sleeping" : "awake");
    return first_error;
}

static esp_err_t init_backlight(void)
{
    /* Match Waveshare's V2 backlight example: configure GPIO42 as an
     * output with pull-up before routing the LEDC signal to it. */
    const gpio_config_t backlight_gpio = {
        .pin_bit_mask = 1ULL << BOARD_LCD_PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_gpio), TAG, "backlight GPIO");

    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_3,
        .freq_hz = 50000,
        .clk_cfg = LEDC_SLOW_CLK_RC_FAST,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");

    const ledc_channel_config_t channel = {
        .gpio_num = BOARD_LCD_PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_3,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel);
}

static esp_err_t init_touch(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_TOUCH_I2C_PORT,
        .sda_io_num = BOARD_TOUCH_PIN_SDA,
        .scl_io_num = BOARD_TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_touch_bus), TAG, "touch I2C bus");

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_TOUCH_I2C_ADDRESS,
        .scl_speed_hz = 300000,
    };
    return i2c_master_bus_add_device(s_touch_bus, &device_config, &s_touch_device);
}

static esp_err_t init_panel(lv_display_t **lvgl_display)
{
    const spi_bus_config_t bus_config = {
        .data0_io_num = BOARD_LCD_PIN_DATA0,
        .data1_io_num = BOARD_LCD_PIN_DATA1,
        .sclk_io_num = BOARD_LCD_PIN_PCLK,
        .data2_io_num = BOARD_LCD_PIN_DATA2,
        .data3_io_num = BOARD_LCD_PIN_DATA3,
        .max_transfer_sz = BOARD_DMA_BUFFER_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG,
                        "LCD QSPI bus");

    esp_lcd_panel_io_handle_t panel_io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .dc_gpio_num = -1,
        .spi_mode = 3,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_transfer_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(BOARD_LCD_HOST, &io_config, &panel_io), TAG,
                        "LCD panel IO");

    const axs15231b_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_commands,
        .init_cmds_size = sizeof(s_lcd_init_commands) / sizeof(s_lcd_init_commands[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel), TAG,
                        "AXS15231B panel");

    ESP_RETURN_ON_ERROR(board_power_reset_display(), TAG, "V2 LCD reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");

    lv_display_t *display = lv_display_create(BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_NO_MEM, TAG, "LVGL display");
    lv_display_set_flush_cb(display, lvgl_flush);
    lv_display_set_user_data(display, panel);

    uint8_t *frame_a = heap_caps_malloc(BOARD_FRAME_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *frame_b = heap_caps_malloc(BOARD_FRAME_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rotated_buffer = heap_caps_malloc(BOARD_FRAME_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_dma_buffer = heap_caps_malloc(BOARD_DMA_BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(frame_a && frame_b && s_rotated_buffer && s_dma_buffer, ESP_ERR_NO_MEM, TAG,
                        "display buffers");
    lv_display_set_buffers(display, frame_a, frame_b, BOARD_FRAME_BUFFER_BYTES,
                           LV_DISPLAY_RENDER_MODE_FULL);
    /*
     * The panel is mounted with the USB connector on the opposite side from
     * the preferred desktop orientation. 270 degrees keeps the 640 x 172
     * landscape layout while rotating both rendering and LVGL's pointer
     * transform by 180 degrees compared with the previous 90 degree setup.
     */
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    /* LVGL defaults to a 33 ms refresh period (about 30 FPS). Scrolling a
     * full-height carousel benefits from requesting the next frame at 16 ms;
     * the display driver will naturally run slower if a transfer takes more
     * time, without queuing stale frames. */
    lv_timer_set_period(lv_display_get_refr_timer(display), BOARD_LVGL_REFRESH_MS);
    s_panel = panel;
    *lvgl_display = display;
    return ESP_OK;
}

esp_err_t board_display_init(void)
{
    ESP_RETURN_ON_ERROR(board_power_prepare_display(), TAG, "V2 display power");
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(init_touch(), TAG, "touch");

    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done != NULL, ESP_ERR_NO_MEM, TAG, "flush semaphore");

    lv_init();
    lv_display_t *display = NULL;
    ESP_RETURN_ON_ERROR(init_panel(&display), TAG, "display");

    lv_indev_t *input = lv_indev_create();
    ESP_RETURN_ON_FALSE(input != NULL, ESP_ERR_NO_MEM, TAG, "LVGL input");
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_read_cb(input, touch_read);
    lv_timer_set_period(lv_indev_get_read_timer(input), 10);
    /* Start scrolling after a small deliberate move and make the release
     * inertia settle quickly on this short, wide screen. */
    lv_indev_set_scroll_limit(input, 6);
    lv_indev_set_scroll_throw(input, 20);
    s_touch_input = input;

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "LVGL tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, BOARD_LVGL_TICK_MS * 1000), TAG,
                        "start LVGL tick");

    s_lvgl_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl_mutex != NULL, ESP_ERR_NO_MEM, TAG, "LVGL mutex");
    const BaseType_t created = xTaskCreatePinnedToCore(
        lvgl_task, "lvgl", BOARD_LVGL_TASK_STACK, NULL, BOARD_LVGL_TASK_PRIORITY, NULL,
        BOARD_LVGL_TASK_CORE);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "LVGL task");

    ESP_LOGI(TAG, "640x172 landscape display ready (180 degree orientation)");
    return ESP_OK;
}
