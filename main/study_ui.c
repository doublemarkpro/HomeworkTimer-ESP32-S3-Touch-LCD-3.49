#include "study_ui.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alarm_audio.h"
#include "alarm_store.h"
#include "app_settings.h"
#include "assets/ui_icons.h"
#include "battery_monitor.h"
#include "board_display.h"
#include "board_power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "rtc_pcf85063.h"
#include "study_store.h"
#include "wifi_manager.h"
#include "weather_manager.h"

LV_FONT_DECLARE(ui_font_16);
LV_FONT_DECLARE(ui_font_bold_16);
LV_FONT_DECLARE(ui_font_bold_20);
LV_FONT_DECLARE(ui_font_clock_bold_56);
LV_FONT_DECLARE(ui_font_title_24);

#define FONT_CJK (&ui_font_16)
#define FONT_CJK_SMALL_BOLD (&ui_font_bold_16)
#define FONT_CJK_BOLD (&ui_font_bold_20)
#define FONT_CJK_TITLE (&ui_font_title_24)
#define FONT_TIMER_CJK FONT_CJK_BOLD
#define FONT_LOCK_CLOCK (&ui_font_clock_bold_56)

#if LV_FONT_MONTSERRAT_48
#define FONT_TIMER (&lv_font_montserrat_48)
#else
#define FONT_TIMER LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_32
#define FONT_CLOCK (&lv_font_montserrat_32)
#else
#define FONT_CLOCK LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_20
#define FONT_VALUE (&lv_font_montserrat_20)
#else
#define FONT_VALUE LV_FONT_DEFAULT
#endif

#define COLOR_BG          0x030A18
#define COLOR_BG_GRAD     0x071A35
#define COLOR_PANEL       0x10233F
#define COLOR_PANEL_DARK  0x09172B
#define COLOR_TEXT        0xF7F9FD
#define COLOR_MUTED       0x91A8C9
#define COLOR_CHINESE     0x397EE8
#define COLOR_CHINESE_2   0x173E78
#define COLOR_MATH        0xF1B914
#define COLOR_MATH_2      0x775000
#define COLOR_ENGLISH     0x8068E3
#define COLOR_ENGLISH_2   0x302A6B
#define COLOR_BREAK       0x39CBA7
#define COLOR_BREAK_2     0x0C5A58
#define COLOR_DANGER      0xF16E75
#define COLOR_DANGER_2    0x983B45
#define COLOR_WIFI        0x36B8C7
#define COLOR_WIFI_2      0x0A4E60
#define COLOR_WEATHER     0x39BCE7
#define COLOR_WEATHER_2   0x0A416B

typedef enum {
    SCREEN_HOME,
    SCREEN_WEATHER,
    SCREEN_TIMER,
    SCREEN_REPORT,
    SCREEN_MENU,
    SCREEN_SETTINGS,
    SCREEN_FOCUS_SETTINGS,
    SCREEN_SCHEDULE,
    SCREEN_WIFI,
    SCREEN_WIFI_PASSWORD,
    SCREEN_ALARM,
    SCREEN_ALARM_RING,
    SCREEN_LOCK,
} screen_id_t;

typedef struct {
    study_subject_t subject;
    bool active;
    bool running;
    uint32_t accumulated_seconds;
    int64_t started_ms;
} active_session_t;

typedef enum {
    ALARM_HOUR_DOWN = 0,
    ALARM_HOUR_UP,
    ALARM_MINUTE_DOWN,
    ALARM_MINUTE_UP,
} alarm_adjust_t;

static const char *TAG = "study_ui";
static const char *const s_subject_names[] = {"语文", "数学", "英语", "休息"};
static const uint32_t s_subject_colors[] = {
    COLOR_CHINESE, COLOR_MATH, COLOR_ENGLISH, COLOR_BREAK,
};
static const uint32_t s_subject_dark_colors[] = {
    COLOR_CHINESE_2, COLOR_MATH_2, COLOR_ENGLISH_2, COLOR_BREAK_2,
};
static const char *const s_weekday_names[] = {
    "周一", "周二", "周三", "周四", "周五", "周六", "周日",
};
static const char *const s_schedule[5][7] = {
    {"语文", "数学", "英语", "信息", "语文", "科学", "班会"},
    {"数学", "英语", "语文", "音乐", "美术", "选修", "选修"},
    {"数学", "语文", "英语", "体育", "语文", "语文", "数学"},
    {"语文", "英语", "数学", "体育", "劳动", "美术", "科学"},
    {"英语", "数学", "语文", "武术", "竖笛", "数学", "—"},
};

static const char *const s_keyboard_lower_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_BACKSPACE, "\n",
    "z", "x", "c", "v", "b", "n", "m", ".", "-", "\n",
    "ABC", "1#", " ", LV_SYMBOL_OK, "",
};

static const char *const s_keyboard_upper_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_BACKSPACE, "\n",
    "Z", "X", "C", "V", "B", "N", "M", ".", "-", "\n",
    "abc", "1#", " ", LV_SYMBOL_OK, "",
};

static const char *const s_keyboard_special_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
    "_", "-", "+", "=", "/", "\\", ":", ";", "?", "~", "\n",
    ".", ",", "\"", "'", "[", "]", "{", "}", LV_SYMBOL_BACKSPACE, "\n",
    "abc", " ", LV_SYMBOL_OK, "",
};

static const lv_buttonmatrix_ctrl_t s_keyboard_ctrl_map[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    6, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
};

static const lv_buttonmatrix_ctrl_t s_keyboard_special_ctrl_map[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, 8, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
};

static active_session_t s_session;
static screen_id_t s_screen;
static rtc_datetime_t s_now;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_timer_echo_label;
static lv_obj_t *s_timer_status_label;
static lv_obj_t *s_timer_status_dot;
static lv_obj_t *s_pause_label;
static lv_obj_t *s_pause_icon;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_password_textarea;
static lv_obj_t *s_alarm_switch;
static lv_obj_t *s_alarm_status_label;
static lv_obj_t *s_volume_label;
static lv_obj_t *s_brightness_label;
static lv_obj_t *s_auto_lock_label;
static lv_obj_t *s_focus_goal_labels[3];
static lv_obj_t *s_lock_clock_label;
static lv_obj_t *s_lock_status_label;
static lv_obj_t *s_home_wifi_icon;
static lv_obj_t *s_home_battery_icon;
static lv_obj_t *s_home_charge_icon;
static lv_obj_t *s_home_battery_text;
static lv_obj_t *s_weather_status_label;
static lv_obj_t *s_weather_day_images[WEATHER_FORECAST_DAYS];
static lv_obj_t *s_weather_day_dates[WEATHER_FORECAST_DAYS];
static lv_obj_t *s_weather_day_conditions[WEATHER_FORECAST_DAYS];
static lv_obj_t *s_weather_day_temperatures[WEATHER_FORECAST_DAYS];
static uint32_t s_weather_generation;
static uint32_t s_last_rendered_second = UINT32_MAX;
static uint32_t s_timer_target_seconds = 45U * 60U;
static uint8_t s_last_clock_minute = UINT8_MAX;
static int64_t s_last_rtc_poll_ms;
static int64_t s_last_alarm_key = INT64_MIN;
static int64_t s_snooze_due_ms;
static bool s_snooze_active;
static screen_id_t s_alarm_return_screen;
static uint8_t s_schedule_day;
static uint32_t s_wifi_generation;
static wifi_manager_network_t s_wifi_page_networks[WIFI_MANAGER_MAX_NETWORKS];
static size_t s_wifi_page_count;
static char s_selected_ssid[33];
static bool s_schedule_drag_active;
static lv_point_t s_schedule_drag_start;
static int64_t s_last_battery_poll_ms;
static int16_t s_displayed_battery_percent = -1;
static bool s_last_usb_powered;
static bool s_usb_state_known;
static bool s_locked;
static bool s_display_sleeping;
static bool s_unlock_long_press_confirmed;
static screen_id_t s_lock_return_screen = SCREEN_HOME;
static int64_t s_lock_awake_deadline_ms;

static void show_home(void);
static void show_weather(void);
static void show_timer(void);
static void show_report(void);
static void show_menu(void);
static void show_settings(void);
static void show_focus_settings(void);
static void show_schedule(void);
static void show_wifi(bool start_scan);
static void show_wifi_password(void);
static void show_alarm(void);
static void show_alarm_ring(void);
static void show_lock_screen(void);
static void enter_lock_mode(void);

static int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t session_elapsed_seconds(void)
{
    if (!s_session.active) {
        return 0;
    }
    uint32_t elapsed = s_session.accumulated_seconds;
    if (s_session.running) {
        elapsed += (uint32_t)((uptime_ms() - s_session.started_ms) / 1000);
    }
    return elapsed;
}

static void format_timer(uint32_t seconds, char *buffer, size_t length)
{
    const uint32_t shown = seconds > 5999U ? 5999U : seconds;
    snprintf(buffer, length, "%02" PRIu32 ":%02" PRIu32,
             shown / 60U, shown % 60U);
}

static void format_minutes(uint32_t seconds, char *buffer, size_t length)
{
    const uint32_t minutes = (seconds + 30U) / 60U;
    if (minutes < 60U) {
        snprintf(buffer, length, "%" PRIu32 "分钟", minutes);
    } else {
        snprintf(buffer, length, "%" PRIu32 "时%02" PRIu32 "分钟", minutes / 60U,
                 minutes % 60U);
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, int x, int y, int width, int height,
                            lv_text_align_t alignment)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, alignment, 0);
    return label;
}

static void style_surface(lv_obj_t *object, uint32_t color, uint32_t dark_color, int radius,
                          int border_width)
{
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(dark_color), 0);
    lv_obj_set_style_bg_grad_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, border_width, 0);
    lv_obj_set_style_border_color(object, lv_color_lighten(lv_color_hex(color), 32), 0);
    lv_obj_set_style_shadow_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(object, 8, 0);
    lv_obj_set_style_shadow_opa(object, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

static void touch_sound_event(lv_event_t *event)
{
    (void)event;
    if (app_settings_volume() >= 10) {
        alarm_audio_click();
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t color,
                             uint32_t dark_color, int x, int y, int width, int height,
                             lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    style_surface(button, color, dark_color, 13, 2);
    lv_obj_add_event_cb(button, touch_sound_event, LV_EVENT_PRESSED, NULL);
    if (callback != NULL) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_PRESSED, user_data);
    }
    lv_obj_t *label = lv_label_create(button);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, width - 12);
    const lv_font_t *button_font = width >= 72 && height >= 35 ? FONT_CJK_BOLD : FONT_CJK;
    lv_obj_set_style_text_font(label, button_font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int width, int height,
                            uint32_t color, uint32_t dark_color)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    style_surface(panel, color, dark_color, 12, 1);
    return panel;
}

static lv_obj_t *make_shape(lv_obj_t *parent, int x, int y, int width, int height,
                            uint32_t color, lv_opa_t opacity, int radius)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(shape, opacity, 0);
    lv_obj_set_style_border_width(shape, 0, 0);
    lv_obj_set_style_pad_all(shape, 0, 0);
    return shape;
}

static lv_obj_t *add_image_icon(lv_obj_t *parent, const lv_image_dsc_t *source,
                                int x, int y)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

static const lv_image_dsc_t *subject_icon(study_subject_t subject)
{
    switch (subject) {
        case STUDY_SUBJECT_CHINESE:
            return &ui_icon_book;
        case STUDY_SUBJECT_MATH:
            return &ui_icon_compass;
        case STUDY_SUBJECT_ENGLISH:
            return &ui_icon_english;
        default:
            return &ui_icon_cup;
    }
}

static void add_book_icon(lv_obj_t *card)
{
    add_image_icon(card, &ui_icon_book, 23, 4);
}

static void add_compass_icon(lv_obj_t *card)
{
    add_image_icon(card, &ui_icon_compass, 23, 4);
}

static void add_english_icon(lv_obj_t *card)
{
    add_image_icon(card, &ui_icon_english, 23, 4);
}

static void add_cup_icon(lv_obj_t *card)
{
    add_image_icon(card, &ui_icon_cup, 27, 8);
}

static lv_obj_t *make_subject_card(lv_obj_t *parent, uint8_t subject, int x,
                                   uint32_t weekly_minutes, lv_event_cb_t callback)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_pos(card, x, 10);
    lv_obj_set_size(card, 110, 152);
    style_surface(card, s_subject_colors[subject], s_subject_dark_colors[subject], 14, 2);
    lv_obj_add_event_cb(card, touch_sound_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(card, callback, LV_EVENT_CLICKED, (void *)(intptr_t)subject);

    if (subject == STUDY_SUBJECT_CHINESE) {
        add_book_icon(card);
    } else if (subject == STUDY_SUBJECT_MATH) {
        add_compass_icon(card);
    } else {
        add_english_icon(card);
    }

    make_label(card, s_subject_names[subject], FONT_CJK_BOLD, COLOR_TEXT, 0, 76, 110, 27,
               LV_TEXT_ALIGN_CENTER);
    char weekly[32];
    snprintf(weekly, sizeof(weekly), "本周%" PRIu32 "分钟", weekly_minutes);
    make_label(card, weekly, FONT_CJK_SMALL_BOLD, COLOR_TEXT, 0, 112, 110, 23,
               LV_TEXT_ALIGN_CENTER);
    return card;
}

static void add_starfield(lv_obj_t *root)
{
    static const int positions[][2] = {{18, 15}, {92, 9}, {178, 39}, {312, 12},
                                       {426, 25}, {533, 8}, {612, 34}, {76, 147}};
    for (size_t index = 0; index < sizeof(positions) / sizeof(positions[0]); ++index) {
        lv_obj_t *star = lv_obj_create(root);
        lv_obj_remove_flag(star, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(star, positions[index][0], positions[index][1]);
        lv_obj_set_size(star, index % 3 == 0 ? 4 : 3, index % 3 == 0 ? 4 : 3);
        lv_obj_set_style_radius(star, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(star, lv_color_hex(index % 2 == 0 ? 0x4D87DD : 0x36C6B6), 0);
        lv_obj_set_style_bg_opa(star, LV_OPA_60, 0);
        lv_obj_set_style_border_width(star, 0, 0);
    }
}

static lv_obj_t *prepare_screen(void)
{
    lv_obj_t *root = lv_screen_active();
    lv_obj_clean(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(COLOR_BG_GRAD), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    add_starfield(root);
    s_clock_label = NULL;
    s_timer_label = NULL;
    s_timer_echo_label = NULL;
    s_timer_status_label = NULL;
    s_timer_status_dot = NULL;
    s_pause_label = NULL;
    s_pause_icon = NULL;
    s_progress_bar = NULL;
    s_password_textarea = NULL;
    s_alarm_switch = NULL;
    s_alarm_status_label = NULL;
    s_volume_label = NULL;
    s_brightness_label = NULL;
    s_auto_lock_label = NULL;
    memset(s_focus_goal_labels, 0, sizeof(s_focus_goal_labels));
    s_lock_clock_label = NULL;
    s_lock_status_label = NULL;
    s_home_wifi_icon = NULL;
    s_home_battery_icon = NULL;
    s_home_charge_icon = NULL;
    s_home_battery_text = NULL;
    s_weather_status_label = NULL;
    memset(s_weather_day_images, 0, sizeof(s_weather_day_images));
    memset(s_weather_day_dates, 0, sizeof(s_weather_day_dates));
    memset(s_weather_day_conditions, 0, sizeof(s_weather_day_conditions));
    memset(s_weather_day_temperatures, 0, sizeof(s_weather_day_temperatures));
    return root;
}

static void home_event(lv_event_t *event)
{
    (void)event;
    show_home();
}

static void menu_event(lv_event_t *event)
{
    (void)event;
    show_menu();
}

static void settings_event(lv_event_t *event)
{
    (void)event;
    show_settings();
}

static void focus_settings_event(lv_event_t *event)
{
    (void)event;
    show_focus_settings();
}

static void focus_goal_adjust_event(lv_event_t *event)
{
    const intptr_t code = (intptr_t)lv_event_get_user_data(event);
    const uint8_t subject = (uint8_t)(code / 2);
    const bool increase = (code & 1) != 0;
    const uint8_t current = app_settings_focus_minutes(subject);
    const uint8_t next = increase ? (current >= 90 ? 90 : current + 5)
                                  : (current <= 10 ? 10 : current - 5);
    if (next == current) {
        return;
    }
    const esp_err_t err = app_settings_set_focus_minutes(subject, next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save focus duration failed: %s", esp_err_to_name(err));
        return;
    }
    if (s_focus_goal_labels[subject] != NULL) {
        char value[16];
        snprintf(value, sizeof(value), "%u分", (unsigned)next);
        lv_label_set_text(s_focus_goal_labels[subject], value);
    }
}

static void start_subject_event(lv_event_t *event)
{
    s_session.subject = (study_subject_t)(intptr_t)lv_event_get_user_data(event);
    s_session.active = true;
    s_session.running = true;
    s_session.accumulated_seconds = 0;
    s_session.started_ms = uptime_ms();
    s_last_rendered_second = UINT32_MAX;
    show_timer();
}

static void pause_event(lv_event_t *event)
{
    (void)event;
    if (!s_session.active) {
        return;
    }
    if (s_session.running) {
        s_session.accumulated_seconds = session_elapsed_seconds();
        s_session.running = false;
    } else {
        s_session.started_ms = uptime_ms();
        s_session.running = true;
    }
    lv_label_set_text(s_pause_label, s_session.running ? "暂停" : "继续");
    lv_label_set_text(s_timer_status_label, s_session.running ? "正在计时" : "已暂停");
    lv_label_set_text(s_pause_icon, s_session.running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_bg_color(s_timer_status_dot,
                              lv_color_hex(s_session.running
                                               ? s_subject_colors[s_session.subject]
                                               : COLOR_MUTED),
                              0);
}

static void finish_event(lv_event_t *event)
{
    (void)event;
    if (!s_session.active) {
        return;
    }
    const uint32_t elapsed = session_elapsed_seconds();
    if (!rtc_pcf85063_read(&s_now)) {
        ESP_LOGW(TAG, "RTC read failed; using last valid date");
    }
    const esp_err_t err = study_store_add_session(s_session.subject, elapsed, &s_now);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save session: %s", esp_err_to_name(err));
    }
    s_session = (active_session_t){0};
    show_home();
}

static void report_event(lv_event_t *event)
{
    (void)event;
    show_report();
}

static void schedule_event(lv_event_t *event)
{
    (void)event;
    const int32_t civil_day = study_days_from_civil(s_now.year, s_now.month, s_now.day);
    const uint8_t weekday = study_monday_weekday(civil_day);
    s_schedule_day = weekday < 5 ? weekday : 0;
    show_schedule();
}

static void alarm_event(lv_event_t *event)
{
    (void)event;
    show_alarm();
}

static void wifi_event(lv_event_t *event)
{
    (void)event;
    show_wifi(true);
}

static const lv_image_dsc_t *weather_icon(uint16_t code)
{
    if (code == 100 || code == 150 || code == 900) {
        return &ui_icon_weather_sunny;
    }
    if ((code >= 101 && code <= 103) || (code >= 151 && code <= 153)) {
        return &ui_icon_weather_partly_cloudy;
    }
    if (code == 104) {
        return &ui_icon_weather_cloudy;
    }
    if (code >= 302 && code <= 304) {
        return &ui_icon_weather_thunder;
    }
    if ((code >= 300 && code <= 399)) {
        return &ui_icon_weather_rain;
    }
    if (code >= 400 && code <= 499) {
        return &ui_icon_weather_snow;
    }
    if (code >= 500 && code <= 515) {
        return &ui_icon_weather_fog;
    }
    return &ui_icon_weather_cloudy;
}

static const char *weather_condition_name(uint16_t code)
{
    if (code == 100 || code == 150 || code == 900) {
        return "晴";
    }
    if ((code >= 101 && code <= 103) || (code >= 151 && code <= 153)) {
        return "多云";
    }
    if (code == 104) {
        return "阴";
    }
    if (code >= 302 && code <= 304) {
        return "雷雨";
    }
    if (code >= 300 && code <= 399) {
        return "雨";
    }
    if (code >= 400 && code <= 499) {
        return "雪";
    }
    if (code >= 500 && code <= 515) {
        return "雾";
    }
    return "天气";
}

static void update_weather_page(bool force)
{
    if (s_screen != SCREEN_WEATHER || s_weather_status_label == NULL) {
        return;
    }
    weather_status_t status;
    weather_manager_get_status(&status);
    if (!force && status.generation == s_weather_generation) {
        return;
    }
    s_weather_generation = status.generation;
    const bool ready = status.state == WEATHER_READY &&
                       status.day_count == WEATHER_FORECAST_DAYS;
    const char *state_text = "";
    if (!ready) {
        if (status.state == WEATHER_CONFIG_REQUIRED) {
            state_text = "天气未配置";
        } else if (status.state == WEATHER_WAITING_FOR_WIFI) {
            state_text = "等待网络连接";
        } else if (status.state == WEATHER_LOADING) {
            state_text = "正在更新天气";
        } else {
            state_text = "天气更新失败";
        }
    }
    lv_label_set_text(s_weather_status_label, state_text);
    lv_obj_set_style_text_color(s_weather_status_label,
                                lv_color_hex(ready ? COLOR_BREAK : COLOR_MUTED), 0);

    static const char *const day_names[WEATHER_FORECAST_DAYS] = {
        "今天", "明天", "后天",
    };
    for (uint8_t index = 0; index < WEATHER_FORECAST_DAYS; ++index) {
        char date_text[20];
        char temperature_text[24];
        if (ready) {
            const weather_forecast_day_t *day = &status.days[index];
            snprintf(date_text, sizeof(date_text), "%s %u/%u", day_names[index],
                     (unsigned)day->month, (unsigned)day->day);
            snprintf(temperature_text, sizeof(temperature_text), "%d ~ %d C",
                     (int)day->temperature_min_c, (int)day->temperature_max_c);
            lv_image_set_src(s_weather_day_images[index], weather_icon(day->icon_code));
            lv_label_set_text(s_weather_day_conditions[index],
                              weather_condition_name(day->icon_code));
            lv_obj_set_style_text_color(s_weather_day_temperatures[index],
                                        lv_color_hex(COLOR_TEXT), 0);
        } else {
            snprintf(date_text, sizeof(date_text), "%s", day_names[index]);
            snprintf(temperature_text, sizeof(temperature_text), "-- ~ -- C");
            lv_image_set_src(s_weather_day_images[index], &ui_icon_weather_cloudy);
            lv_label_set_text(s_weather_day_conditions[index], "等待中");
            lv_obj_set_style_text_color(s_weather_day_temperatures[index],
                                        lv_color_hex(COLOR_MUTED), 0);
        }
        lv_label_set_text(s_weather_day_dates[index], date_text);
        lv_label_set_text(s_weather_day_temperatures[index], temperature_text);
    }
}

static void weather_event(lv_event_t *event)
{
    (void)event;
    show_weather();
    weather_status_t status;
    weather_manager_get_status(&status);
    if (status.state != WEATHER_READY && status.state != WEATHER_LOADING) {
        weather_manager_refresh();
    }
}

static void weather_refresh_event(lv_event_t *event)
{
    (void)event;
    weather_status_t status;
    weather_manager_get_status(&status);
    if (status.state == WEATHER_CONFIG_REQUIRED) {
        if (s_weather_status_label != NULL) {
            lv_label_set_text(s_weather_status_label, "天气未配置");
        }
        return;
    }
    if (s_weather_status_label != NULL) {
        lv_label_set_text(s_weather_status_label, "正在更新天气");
        lv_obj_set_style_text_color(s_weather_status_label,
                                    lv_color_hex(COLOR_MUTED), 0);
    }
    weather_manager_refresh();
}

static void update_home_status_icons(bool force_battery)
{
    if (s_screen != SCREEN_HOME || s_home_wifi_icon == NULL) {
        return;
    }

    wifi_manager_status_t wifi_status;
    wifi_manager_get_status(&wifi_status);
    const uint32_t wifi_color = wifi_status.state == WIFI_MANAGER_CONNECTED
                                    ? COLOR_BREAK
                                    : (wifi_status.state == WIFI_MANAGER_CONNECTING ||
                                               wifi_status.state == WIFI_MANAGER_SCANNING
                                           ? COLOR_WIFI
                                           : COLOR_MUTED);
    lv_obj_set_style_text_color(s_home_wifi_icon, lv_color_hex(wifi_color), 0);

    const bool usb_powered = battery_monitor_usb_powered();
    const uint32_t power_color = usb_powered ? COLOR_CHINESE : COLOR_BREAK;
    if (s_home_charge_icon != NULL) {
        lv_label_set_text(s_home_charge_icon, usb_powered ? LV_SYMBOL_CHARGE : "");
        lv_obj_set_style_text_color(s_home_charge_icon, lv_color_hex(power_color), 0);
    }
    if (s_home_battery_icon != NULL) {
        lv_obj_set_style_text_color(s_home_battery_icon, lv_color_hex(power_color), 0);
    }
    if (s_home_battery_text != NULL) {
        lv_obj_set_style_text_color(s_home_battery_text, lv_color_hex(power_color), 0);
    }

    const int64_t now_ms = uptime_ms();
    if (!force_battery && now_ms - s_last_battery_poll_ms < 10000) {
        return;
    }
    s_last_battery_poll_ms = now_ms;

    uint16_t millivolts = 0;
    uint8_t percent = 0;
    if (!battery_monitor_read(&millivolts, &percent)) {
        lv_label_set_text(s_home_battery_icon, LV_SYMBOL_BATTERY_EMPTY);
        lv_label_set_text(s_home_battery_text, "--");
        lv_obj_set_style_text_color(s_home_battery_icon, lv_color_hex(COLOR_MUTED), 0);
        return;
    }

    /* Voltage can legitimately move in either direction after load changes.
     * The old USB-dependent one-way clamp could permanently pin an inaccurate
     * value. Apply a small hysteresis and move one point per sample instead. */
    if (s_displayed_battery_percent < 0 || !s_usb_state_known ||
        usb_powered != s_last_usb_powered) {
        s_displayed_battery_percent = percent;
    } else if (percent >= s_displayed_battery_percent + 2) {
        ++s_displayed_battery_percent;
    } else if (percent + 2 <= s_displayed_battery_percent) {
        --s_displayed_battery_percent;
    }
    s_usb_state_known = true;
    s_last_usb_powered = usb_powered;
    percent = (uint8_t)s_displayed_battery_percent;

    const char *symbol = LV_SYMBOL_BATTERY_EMPTY;
    if (percent >= 88) {
        symbol = LV_SYMBOL_BATTERY_FULL;
    } else if (percent >= 63) {
        symbol = LV_SYMBOL_BATTERY_3;
    } else if (percent >= 38) {
        symbol = LV_SYMBOL_BATTERY_2;
    } else if (percent >= 13) {
        symbol = LV_SYMBOL_BATTERY_1;
    }
    char percentage[8];
    snprintf(percentage, sizeof(percentage), "%u%%", percent);
    lv_label_set_text(s_home_battery_icon, symbol);
    lv_label_set_text(s_home_battery_text, percentage);
    lv_obj_set_style_text_color(s_home_battery_icon, lv_color_hex(power_color), 0);
    ESP_LOGD(TAG, "battery %u mV (%u%%), USB %s", millivolts, percent,
             usb_powered ? "connected" : "disconnected");
}

static void show_home(void)
{
    if (rtc_pcf85063_read(&s_now)) {
        study_store_refresh_period(&s_now);
    }
    const study_week_t *week = study_store_current();
    s_screen = SCREEN_HOME;
    lv_obj_t *root = prepare_screen();

    make_label(root, "开始写作业", FONT_CJK_TITLE, COLOR_TEXT, 8, 7, 135, 28,
               LV_TEXT_ALIGN_CENTER);
    char clock_text[8];
    snprintf(clock_text, sizeof(clock_text), "%02u:%02u", s_now.hour, s_now.minute);
    s_clock_label = make_label(root, clock_text, FONT_CLOCK, COLOR_TEXT, 8, 37, 135, 38,
                               LV_TEXT_ALIGN_CENTER);
    char date_text[32];
    const int32_t civil_day = study_days_from_civil(s_now.year, s_now.month, s_now.day);
    const uint8_t weekday = study_monday_weekday(civil_day);
    snprintf(date_text, sizeof(date_text), "%u月%u日 %s", s_now.month, s_now.day,
             s_weekday_names[weekday]);
    make_label(root, date_text, FONT_CJK, COLOR_MUTED, 8, 77, 135, 22, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *divider = make_shape(root, 23, 106, 105, 2, COLOR_CHINESE, LV_OPA_30, 1);
    lv_obj_move_background(divider);

    const char *status = (week->completion_mask & 0x07) == 0x07
                             ? "全部完成 真棒!"
                             : (weekday == 4 ? "周五 查看周报" : "选择科目开始");
    make_label(root, status, FONT_CJK, COLOR_BREAK, 8, 115, 135, 23, LV_TEXT_ALIGN_CENTER);

    s_home_wifi_icon = make_label(root, LV_SYMBOL_WIFI, LV_FONT_DEFAULT, COLOR_MUTED,
                                  25, 145, 25, 20, LV_TEXT_ALIGN_CENTER);
    s_home_battery_icon = make_label(root, LV_SYMBOL_BATTERY_EMPTY, LV_FONT_DEFAULT, COLOR_MUTED,
                                     57, 145, 25, 20, LV_TEXT_ALIGN_CENTER);
    s_home_charge_icon = make_label(root, "", LV_FONT_DEFAULT, COLOR_CHINESE,
                                    80, 145, 16, 20, LV_TEXT_ALIGN_CENTER);
    s_home_battery_text = make_label(root, "--", LV_FONT_DEFAULT, COLOR_MUTED,
                                     97, 145, 36, 20, LV_TEXT_ALIGN_LEFT);
    update_home_status_icons(true);

    lv_obj_t *carousel = lv_obj_create(root);
    lv_obj_set_pos(carousel, 146, 0);
    lv_obj_set_size(carousel, 494, 172);
    lv_obj_set_style_bg_opa(carousel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(carousel, 0, 0);
    lv_obj_set_style_pad_all(carousel, 0, 0);
    lv_obj_set_scroll_dir(carousel, LV_DIR_HOR);
    /* The viewport already exposes more than four cards. Snapping every
     * 118 px made short swipes animate back in the opposite direction and
     * SCROLL_ONE added artificial resistance. Keep a short natural coast,
     * but let the content track the finger and stop firmly at both ends. */
    lv_obj_set_scroll_snap_x(carousel, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scroll_snap_y(carousel, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scrollbar_mode(carousel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(carousel, LV_OBJ_FLAG_SCROLL_ONE | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_flag(carousel, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    static const int subject_x[] = {0, 118, 236};
    for (uint8_t index = 0; index < 3; ++index) {
        make_subject_card(carousel, index, subject_x[index],
                          (week->subject_seconds[index] + 30U) / 60U,
                          start_subject_event);
    }

    lv_obj_t *break_card = lv_button_create(carousel);
    lv_obj_set_pos(break_card, 354, 10);
    lv_obj_set_size(break_card, 110, 152);
    style_surface(break_card, COLOR_BREAK, COLOR_BREAK_2, 14, 2);
    lv_obj_add_event_cb(break_card, touch_sound_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(break_card, start_subject_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)STUDY_SUBJECT_BREAK);
    add_cup_icon(break_card);
    make_label(break_card, "休息", FONT_CJK_BOLD, COLOR_TEXT, 0, 76, 110, 27,
               LV_TEXT_ALIGN_CENTER);
    char break_minutes[24];
    snprintf(break_minutes, sizeof(break_minutes), "本周%" PRIu32 "分钟",
             (week->break_seconds + 30U) / 60U);
    make_label(break_card, break_minutes, FONT_CJK_SMALL_BOLD, COLOR_TEXT,
               0, 112, 110, 23,
               LV_TEXT_ALIGN_CENTER);

    lv_obj_t *weather_card = lv_button_create(carousel);
    lv_obj_set_pos(weather_card, 472, 10);
    lv_obj_set_size(weather_card, 110, 152);
    style_surface(weather_card, COLOR_WEATHER, COLOR_WEATHER_2, 14, 2);
    lv_obj_add_event_cb(weather_card, touch_sound_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(weather_card, weather_event, LV_EVENT_CLICKED, NULL);
    add_image_icon(weather_card, &ui_icon_weather_partly_cloudy, 25, 6);
    make_label(weather_card, "天气", FONT_CJK_BOLD, COLOR_TEXT, 0, 76, 110, 27,
               LV_TEXT_ALIGN_CENTER);

    lv_obj_t *more_card = lv_button_create(carousel);
    lv_obj_set_pos(more_card, 590, 10);
    lv_obj_set_size(more_card, 110, 152);
    style_surface(more_card, COLOR_CHINESE, COLOR_PANEL_DARK, 14, 1);
    lv_obj_add_event_cb(more_card, touch_sound_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(more_card, menu_event, LV_EVENT_CLICKED, NULL);
    add_image_icon(more_card, &ui_icon_gear, 31, 16);
    make_label(more_card, "更多", FONT_CJK_BOLD, COLOR_TEXT, 0, 82, 110, 27,
               LV_TEXT_ALIGN_CENTER);
    s_last_clock_minute = s_now.minute;
}

static void show_weather(void)
{
    s_screen = SCREEN_WEATHER;
    lv_obj_t *root = prepare_screen();

    make_label(root, "三日天气", FONT_CJK_BOLD, COLOR_TEXT, 8, 8, 124, 31,
               LV_TEXT_ALIGN_CENTER);
    s_weather_status_label = make_label(root, "正在更新天气", FONT_CJK,
                                        COLOR_MUTED, 8, 40, 124, 24,
                                        LV_TEXT_ALIGN_CENTER);

    make_button(root, "刷新", COLOR_WEATHER, COLOR_WEATHER_2, 12, 66, 108, 42,
                weather_refresh_event, NULL);
    make_button(root, "返回", COLOR_CHINESE, COLOR_CHINESE_2, 12, 116, 108, 44,
                home_event, NULL);

    static const int card_x[WEATHER_FORECAST_DAYS] = {140, 305, 470};
    static const uint32_t card_colors[WEATHER_FORECAST_DAYS] = {
        COLOR_WEATHER, COLOR_CHINESE, COLOR_ENGLISH,
    };
    static const uint32_t card_dark_colors[WEATHER_FORECAST_DAYS] = {
        COLOR_WEATHER_2, COLOR_CHINESE_2, COLOR_ENGLISH_2,
    };
    for (uint8_t index = 0; index < WEATHER_FORECAST_DAYS; ++index) {
        lv_obj_t *card = make_panel(root, card_x[index], 9, 155, 154,
                                    card_colors[index], card_dark_colors[index]);
        s_weather_day_dates[index] = make_label(card, "--", FONT_CJK,
                                                COLOR_TEXT, 5, 5, 145, 26,
                                                LV_TEXT_ALIGN_CENTER);
        s_weather_day_images[index] = add_image_icon(card, &ui_icon_weather_cloudy,
                                                     47, 28);
        s_weather_day_conditions[index] = make_label(card, "等待中", FONT_CJK,
                                                     COLOR_TEXT, 5, 88, 145, 23,
                                                     LV_TEXT_ALIGN_CENTER);
        s_weather_day_temperatures[index] = make_label(card, "-- ~ -- C", FONT_VALUE,
                                                       COLOR_MUTED, 5, 117, 145, 25,
                                                       LV_TEXT_ALIGN_CENTER);
    }
    update_weather_page(true);
}

static void make_timer_subject_icon(lv_obj_t *parent, study_subject_t subject,
                                    uint32_t accent, uint32_t dark)
{
    lv_obj_t *badge = make_panel(parent, 14, 22, 82, 82, accent, dark);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(badge, 3, 0);
    lv_obj_set_style_shadow_width(badge, 12, 0);
    lv_obj_set_style_shadow_opa(badge, LV_OPA_30, 0);
    const lv_image_dsc_t *source = subject_icon(subject);
    lv_obj_t *icon = add_image_icon(badge, source,
                                    (82 - (int)source->header.w) / 2 - 2,
                                    (82 - (int)source->header.h) / 2);
    lv_image_set_scale(icon, (52U * 256U) / source->header.w);
}

static lv_obj_t *make_timer_action_button(lv_obj_t *parent, const char *symbol,
                                          const char *text, uint32_t color,
                                          uint32_t dark, int y,
                                          lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, 478, y);
    lv_obj_set_size(button, 150, 65);
    style_surface(button, color, dark, 13, 2);
    lv_obj_set_style_shadow_width(button, 10, 0);
    lv_obj_set_style_bg_color(button, lv_color_darken(lv_color_hex(dark), 20),
                              LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(button, callback, LV_EVENT_PRESSED, NULL);
    lv_obj_t *icon = make_label(button, symbol, FONT_CLOCK, COLOR_TEXT,
                                12, 15, 34, 36, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *label = make_label(button, text, FONT_TIMER_CJK, COLOR_TEXT,
                                 48, 18, 91, 30, LV_TEXT_ALIGN_CENTER);
    if (callback == pause_event) {
        s_pause_icon = icon;
        s_pause_label = label;
    }
    return button;
}

static void show_timer(void)
{
    s_screen = SCREEN_TIMER;
    lv_obj_t *root = prepare_screen();
    const uint32_t accent = s_subject_colors[s_session.subject];
    const uint32_t dark = s_subject_dark_colors[s_session.subject];
    make_timer_subject_icon(root, s_session.subject, accent, dark);

    char title[24];
    snprintf(title, sizeof(title), "%s%s", s_subject_names[s_session.subject],
             s_session.subject == STUDY_SUBJECT_BREAK ? "时间" : "作业");
    make_label(root, title, FONT_TIMER_CJK, COLOR_TEXT, 108, 28, 94, 30,
               LV_TEXT_ALIGN_LEFT);
    s_timer_status_dot = make_shape(root, 109, 70, 9, 9, accent, LV_OPA_COVER,
                                    LV_RADIUS_CIRCLE);
    s_timer_status_label = make_label(root, "正在计时", FONT_TIMER_CJK, accent, 124, 60,
                                      84, 30, LV_TEXT_ALIGN_LEFT);

    const study_week_t *week = study_store_current();
    char weekly[40];
    if (s_session.subject == STUDY_SUBJECT_BREAK) {
        s_timer_target_seconds = 15U * 60U;
        snprintf(weekly, sizeof(weekly), "本周休息 %" PRIu32 "分",
                 (week->break_seconds + 30U) / 60U);
    } else {
        const uint8_t index = (uint8_t)s_session.subject;
        const uint8_t focus_minutes = app_settings_focus_minutes(index);
        s_timer_target_seconds = (uint32_t)focus_minutes * 60U;
        snprintf(weekly, sizeof(weekly), "今天第%u段 · %u分",
                 (unsigned)(week->sessions[index] + 1),
                 (unsigned)focus_minutes);
    }
    make_label(root, weekly, FONT_TIMER_CJK, COLOR_MUTED, 19, 118, 181, 30,
               LV_TEXT_ALIGN_CENTER);

    s_timer_echo_label = NULL;
    s_timer_label = make_label(root, "00:00", FONT_TIMER, COLOR_TEXT,
                               208, 28, 260, 60, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(s_timer_label, 1, 0);
    s_progress_bar = lv_bar_create(root);
    lv_obj_set_pos(s_progress_bar, 214, 127);
    lv_obj_set_size(s_progress_bar, 244, 10);
    lv_obj_set_style_radius(s_progress_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, 5, LV_PART_INDICATOR);
    lv_bar_set_range(s_progress_bar, 0, (int32_t)s_timer_target_seconds);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x1C3B63), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_progress_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_progress_bar, lv_color_hex(0x315D8A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_progress_bar,
                                   lv_color_lighten(lv_color_hex(accent), 45),
                                   LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_progress_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

    make_timer_action_button(root, LV_SYMBOL_PAUSE, "暂停", COLOR_DANGER,
                             COLOR_DANGER_2, 12, pause_event);
    make_timer_action_button(root, LV_SYMBOL_OK,
                             s_session.subject == STUDY_SUBJECT_BREAK ? "结束休息" : "完成本科",
                             COLOR_CHINESE, COLOR_CHINESE_2, 91, finish_event);
    s_last_rendered_second = UINT32_MAX;
}

static void show_report(void)
{
    if (rtc_pcf85063_read(&s_now)) {
        study_store_refresh_period(&s_now);
    }
    const study_week_t *week = study_store_current();
    s_screen = SCREEN_REPORT;
    lv_obj_t *root = prepare_screen();
    make_label(root, "本周学习报告", FONT_CJK_BOLD, COLOR_TEXT, 8, 14, 132, 29,
               LV_TEXT_ALIGN_CENTER);

    char date_text[32];
    const int32_t civil_day = study_days_from_civil(s_now.year, s_now.month, s_now.day);
    const uint8_t weekday = study_monday_weekday(civil_day);
    snprintf(date_text, sizeof(date_text), "%u月%u日 %s", s_now.month, s_now.day,
             s_weekday_names[weekday]);
    make_label(root, date_text, FONT_CJK, COLOR_CHINESE, 8, 51, 132, 23,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, "每周3次", FONT_CJK, COLOR_MUTED, 8, 78, 132, 23,
               LV_TEXT_ALIGN_CENTER);
    make_button(root, "返回", COLOR_CHINESE, COLOR_CHINESE_2, 20, 115, 108, 39,
                menu_event, NULL);

    make_shape(root, 143, 17, 1, 138, COLOR_MUTED, LV_OPA_30, 1);
    static const int subject_x[] = {151, 249, 347};
    for (uint8_t index = 0; index < 3; ++index) {
        const uint32_t minutes = (week->subject_seconds[index] + 30U) / 60U;
        const uint32_t weekly_goal =
            (uint32_t)app_settings_focus_minutes(index) * 3U;
        lv_obj_t *ring = lv_arc_create(root);
        lv_obj_set_pos(ring, subject_x[index] + 9, 9);
        lv_obj_set_size(ring, 72, 72);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ring,
                                   lv_color_hex(s_subject_dark_colors[index]),
                                   LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(ring, lv_color_hex(s_subject_colors[index]),
                                   LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(ring, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_arc_set_rotation(ring, 270);
        lv_arc_set_bg_angles(ring, 0, 360);
        lv_arc_set_range(ring, 0, (int32_t)weekly_goal);
        lv_arc_set_value(ring,
                         (int32_t)(minutes > weekly_goal ? weekly_goal : minutes));

        const lv_image_dsc_t *icon_source = subject_icon((study_subject_t)index);
        lv_obj_t *icon = add_image_icon(root, icon_source,
                                        subject_x[index] + 13, 13);
        lv_image_set_scale(icon, (48U * 256U) / icon_source->header.w);

        make_label(root, s_subject_names[index], FONT_CJK_BOLD, COLOR_TEXT,
                   subject_x[index], 84, 90, 27,
                   LV_TEXT_ALIGN_CENTER);
        char value[40];
        format_minutes(week->subject_seconds[index], value, sizeof(value));
        make_label(root, value, FONT_CJK, s_subject_colors[index],
                   subject_x[index], 117, 90, 24,
                   LV_TEXT_ALIGN_CENTER);
    }

    make_shape(root, 445, 17, 1, 138, COLOR_MUTED, LV_OPA_30, 1);
    lv_obj_t *focus_icon = add_image_icon(root, &ui_icon_focus, 439, 0);
    lv_image_set_scale(focus_icon, 128);
    make_label(root, "专注", FONT_CJK, COLOR_BREAK, 488, 16, 50, 23,
               LV_TEXT_ALIGN_LEFT);
    char total[40];
    format_minutes(study_store_focus_seconds(), total, sizeof(total));
    make_label(root, total, FONT_CJK_BOLD, COLOR_BREAK, 450, 41, 88, 29,
               LV_TEXT_ALIGN_CENTER);
    make_shape(root, 455, 78, 77, 1, COLOR_MUTED, LV_OPA_30, 1);

    lv_obj_t *small_cup = add_image_icon(root, &ui_icon_cup, 439, 72);
    lv_image_set_scale(small_cup, 128);
    make_label(root, "休息", FONT_CJK, COLOR_BREAK, 488, 91, 50, 23,
               LV_TEXT_ALIGN_LEFT);
    char rest[40];
    format_minutes(week->break_seconds, rest, sizeof(rest));
    make_label(root, rest, FONT_CJK_BOLD, COLOR_BREAK, 450, 116, 88, 29,
               LV_TEXT_ALIGN_CENTER);

    make_shape(root, 542, 17, 1, 138, COLOR_MUTED, LV_OPA_30, 1);
    add_image_icon(root, &ui_icon_star, 556, 5);
    uint32_t session_total = 0;
    for (uint8_t index = 0; index < 3; ++index) {
        session_total += week->sessions[index];
    }
    char reward[24];
    snprintf(reward, sizeof(reward), "完成%" PRIu32 "段", session_total);
    make_label(root, reward, FONT_CJK, COLOR_MATH, 547, 91, 89, 23,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, session_total > 0 ? "真棒!" : "开始学习", FONT_CJK_BOLD,
               COLOR_MATH, 547, 119, 89, 29, LV_TEXT_ALIGN_CENTER);
}

static void show_menu(void)
{
    s_screen = SCREEN_MENU;
    lv_obj_t *root = prepare_screen();
    static const struct {
        const char *text;
        const lv_image_dsc_t *icon;
        uint32_t color;
        uint32_t dark_color;
        lv_event_cb_t callback;
    } cards[] = {
        {"周报", &ui_icon_menu_report, COLOR_CHINESE, COLOR_CHINESE_2, report_event},
        {"课程表", &ui_icon_menu_schedule, COLOR_MATH, COLOR_MATH_2, schedule_event},
        {"闹钟", &ui_icon_menu_alarm, COLOR_ENGLISH, COLOR_ENGLISH_2, alarm_event},
        {"Wi-Fi", &ui_icon_menu_wifi, COLOR_WIFI, COLOR_WIFI_2, wifi_event},
        {"设置", &ui_icon_gear, COLOR_BREAK, COLOR_BREAK_2, settings_event},
        {"返回", &ui_icon_menu_home, COLOR_CHINESE, COLOR_PANEL_DARK, home_event},
    };

    for (size_t index = 0; index < sizeof(cards) / sizeof(cards[0]); ++index) {
        lv_obj_t *card = lv_button_create(root);
        lv_obj_set_pos(card, 8 + (int)index * 104, 10);
        lv_obj_set_size(card, 96, 152);
        style_surface(card, cards[index].color, cards[index].dark_color, 14, 2);
        lv_obj_add_event_cb(card, touch_sound_event, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(card, cards[index].callback, LV_EVENT_CLICKED, NULL);
        add_image_icon(card, cards[index].icon,
                       (96 - (int)cards[index].icon->header.w) / 2, 17);
        make_shape(card, 35, 82, 26, 2, cards[index].color, LV_OPA_70, 1);
        make_label(card, cards[index].text, FONT_CJK_BOLD, COLOR_TEXT, 5, 99, 86, 29,
                   LV_TEXT_ALIGN_CENTER);
    }
}

static void volume_slider_event(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    const uint8_t volume = (uint8_t)lv_slider_get_value(slider);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        alarm_audio_set_volume(volume < 10 ? 0 : volume);
        if (s_volume_label != NULL) {
            char text[20];
            snprintf(text, sizeof(text), "音量 %u%%", volume);
            lv_label_set_text(s_volume_label, text);
        }
    } else if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        const esp_err_t err = app_settings_set_volume(volume);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "save volume failed: %s", esp_err_to_name(err));
        }
        if (volume >= 10) {
            alarm_audio_click();
        }
    }
}

static void brightness_slider_event(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    const uint8_t brightness = (uint8_t)lv_slider_get_value(slider);
    board_display_set_backlight(brightness);
    if (s_brightness_label != NULL) {
        char text[20];
        snprintf(text, sizeof(text), "屏幕 %u%%", brightness);
        lv_label_set_text(s_brightness_label, text);
    }
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        const esp_err_t err = app_settings_set_brightness(brightness);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "save brightness failed: %s", esp_err_to_name(err));
        }
    }
}

static const char *auto_lock_timeout_text(uint8_t minutes)
{
    switch (minutes) {
        case 1:
            return "1分钟";
        case 5:
            return "5分钟";
        case 10:
            return "10分钟";
        case 30:
            return "30分钟";
        default:
            return "永不";
    }
}

static void auto_lock_timeout_event(lv_event_t *event)
{
    (void)event;
    static const uint8_t choices[] = {1, 5, 10, 30, 0};
    const uint8_t current = app_settings_auto_lock_minutes();
    uint8_t next = choices[0];
    for (size_t index = 0; index < sizeof(choices) / sizeof(choices[0]); ++index) {
        if (choices[index] == current) {
            next = choices[(index + 1) % (sizeof(choices) / sizeof(choices[0]))];
            break;
        }
    }
    const esp_err_t err = app_settings_set_auto_lock_minutes(next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save auto lock timeout failed: %s", esp_err_to_name(err));
        return;
    }
    if (s_auto_lock_label != NULL) {
        lv_label_set_text(s_auto_lock_label, auto_lock_timeout_text(next));
    }
    lv_display_trigger_activity(NULL);
}

static void show_settings(void)
{
    s_screen = SCREEN_SETTINGS;
    lv_obj_t *root = prepare_screen();
    static const int card_x[] = {8, 134, 260, 386, 512};

    lv_obj_t *focus_card = lv_button_create(root);
    lv_obj_set_pos(focus_card, card_x[0], 8);
    lv_obj_set_size(focus_card, 120, 156);
    style_surface(focus_card, COLOR_MATH, COLOR_MATH_2, 14, 2);
    lv_obj_add_event_cb(focus_card, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(focus_card, focus_settings_event, LV_EVENT_PRESSED, NULL);
    add_image_icon(focus_card, &ui_icon_settings_duration, 30, 4);
    make_label(focus_card, "每科时长", FONT_CJK_BOLD, COLOR_TEXT,
               4, 66, 112, 26, LV_TEXT_ALIGN_CENTER);
    char focus_values[20];
    snprintf(focus_values, sizeof(focus_values), "%u/%u/%u",
             (unsigned)app_settings_focus_minutes(0),
             (unsigned)app_settings_focus_minutes(1),
             (unsigned)app_settings_focus_minutes(2));
    make_label(focus_card, focus_values, FONT_VALUE, COLOR_TEXT,
               4, 96, 112, 25, LV_TEXT_ALIGN_CENTER);
    make_label(focus_card, "语  数  英", FONT_CJK, COLOR_MATH,
               4, 126, 112, 22, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *volume_card = make_panel(root, card_x[1], 8, 120, 156,
                                       COLOR_BREAK, COLOR_BREAK_2);
    add_image_icon(volume_card, &ui_icon_settings_volume, 30, 5);
    char volume_text[20];
    snprintf(volume_text, sizeof(volume_text), "音量 %u%%", app_settings_volume());
    s_volume_label = make_label(volume_card, volume_text, FONT_CJK_BOLD, COLOR_TEXT,
                                4, 75, 112, 27, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *volume_slider = lv_slider_create(volume_card);
    lv_obj_set_pos(volume_slider, 14, 124);
    lv_obj_set_size(volume_slider, 92, 12);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, app_settings_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_BREAK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(volume_slider, 5, LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider, volume_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_slider_event, LV_EVENT_RELEASED, NULL);

    lv_obj_t *brightness_card = make_panel(root, card_x[2], 8, 120, 156,
                                           COLOR_WEATHER, COLOR_WEATHER_2);
    add_image_icon(brightness_card, &ui_icon_settings_brightness, 30, 5);
    char brightness_text[20];
    snprintf(brightness_text, sizeof(brightness_text), "屏幕 %u%%",
             app_settings_brightness());
    s_brightness_label = make_label(brightness_card, brightness_text, FONT_CJK_BOLD,
                                    COLOR_TEXT, 4, 78, 112, 27, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *brightness_slider = lv_slider_create(brightness_card);
    lv_obj_set_pos(brightness_slider, 14, 132);
    lv_obj_set_size(brightness_slider, 92, 12);
    lv_slider_set_range(brightness_slider, 10, 100);
    lv_slider_set_value(brightness_slider, app_settings_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(COLOR_MATH), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_slider, 5, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_event, LV_EVENT_RELEASED, NULL);

    lv_obj_t *lock_card = make_panel(root, card_x[3], 8, 120, 156,
                                     COLOR_ENGLISH, COLOR_ENGLISH_2);
    add_image_icon(lock_card, &ui_icon_settings_lock, 30, 4);
    make_label(lock_card, "自动锁屏", FONT_CJK_BOLD, COLOR_TEXT,
               4, 72, 112, 27, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *timeout_button = make_button(lock_card,
                                           auto_lock_timeout_text(app_settings_auto_lock_minutes()),
                                           COLOR_WIFI, COLOR_WIFI_2, 10, 111, 100, 34,
                                           auto_lock_timeout_event, NULL);
    s_auto_lock_label = lv_obj_get_child(timeout_button, 0);
    lv_obj_set_style_text_font(s_auto_lock_label, FONT_CJK_SMALL_BOLD, 0);

    lv_obj_t *back_card = lv_button_create(root);
    lv_obj_set_pos(back_card, card_x[4], 8);
    lv_obj_set_size(back_card, 120, 156);
    style_surface(back_card, COLOR_CHINESE, COLOR_CHINESE_2, 14, 2);
    lv_obj_add_event_cb(back_card, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(back_card, menu_event, LV_EVENT_PRESSED, NULL);
    add_image_icon(back_card, &ui_icon_settings_back, 30, 28);
    make_label(back_card, "返回", FONT_CJK_BOLD, COLOR_TEXT,
               4, 104, 112, 30, LV_TEXT_ALIGN_CENTER);
}

static void show_focus_settings(void)
{
    s_screen = SCREEN_FOCUS_SETTINGS;
    lv_obj_t *root = prepare_screen();
    make_label(root, "专注时长", FONT_CJK_BOLD, COLOR_TEXT, 8, 14, 135, 30,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, "每科时间", FONT_CJK, COLOR_MUTED, 8, 54, 135, 24,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, "10-90分", FONT_CJK, COLOR_MATH, 8, 82, 135, 24,
               LV_TEXT_ALIGN_CENTER);
    make_button(root, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 21, 119, 108, 39,
                settings_event, NULL);

    static const int card_x[] = {151, 311, 471};
    for (uint8_t index = 0; index < 3; ++index) {
        lv_obj_t *card = make_panel(root, card_x[index], 8, 150, 156,
                                    s_subject_colors[index],
                                    s_subject_dark_colors[index]);
        const lv_image_dsc_t *source = subject_icon((study_subject_t)index);
        lv_obj_t *icon = add_image_icon(card, source,
                                        (150 - (int)source->header.w) / 2, 2);
        lv_image_set_scale(icon, (42U * 256U) / source->header.w);
        make_label(card, s_subject_names[index], FONT_CJK_BOLD, COLOR_TEXT,
                   0, 53, 150, 28, LV_TEXT_ALIGN_CENTER);
        char value[16];
        snprintf(value, sizeof(value), "%u分",
                 (unsigned)app_settings_focus_minutes(index));
        s_focus_goal_labels[index] = make_label(card, value, FONT_CJK_BOLD,
                                                s_subject_colors[index],
                                                0, 82, 150, 29,
                                                LV_TEXT_ALIGN_CENTER);
        make_button(card, "-", COLOR_PANEL, COLOR_PANEL_DARK, 10, 117, 56, 31,
                    focus_goal_adjust_event,
                    (void *)(intptr_t)(index * 2));
        make_button(card, "+", s_subject_colors[index],
                    s_subject_dark_colors[index], 84, 117, 56, 31,
                    focus_goal_adjust_event,
                    (void *)(intptr_t)(index * 2 + 1));
    }
}

static uint32_t course_color(const char *course)
{
    if (strcmp(course, "数学") == 0 || strcmp(course, "美术") == 0) {
        return COLOR_MATH;
    }
    if (strcmp(course, "英语") == 0 || strcmp(course, "音乐") == 0 ||
        strcmp(course, "竖笛") == 0) {
        return COLOR_ENGLISH;
    }
    if (strcmp(course, "体育") == 0 || strcmp(course, "武术") == 0 ||
        strcmp(course, "劳动") == 0 || strcmp(course, "科学") == 0) {
        return COLOR_BREAK;
    }
    return COLOR_CHINESE;
}

static uint32_t course_dark_color(uint32_t color)
{
    if (color == COLOR_MATH) {
        return COLOR_MATH_2;
    }
    if (color == COLOR_ENGLISH) {
        return COLOR_ENGLISH_2;
    }
    if (color == COLOR_BREAK) {
        return COLOR_BREAK_2;
    }
    return COLOR_CHINESE_2;
}

static uint8_t schedule_month_days(int year, uint8_t month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0)) {
        return 29;
    }
    return days[month - 1];
}

static void schedule_selected_date(uint16_t *year, uint8_t *month, uint8_t *day)
{
    int selected_year = s_now.year;
    int selected_month = s_now.month;
    const int32_t today = study_days_from_civil(s_now.year, s_now.month, s_now.day);
    const int current_weekday = study_monday_weekday(today);
    int selected_day = s_now.day + (int)s_schedule_day - current_weekday;

    while (selected_day < 1) {
        if (--selected_month < 1) {
            selected_month = 12;
            --selected_year;
        }
        selected_day += schedule_month_days(selected_year, (uint8_t)selected_month);
    }
    while (selected_day > schedule_month_days(selected_year, (uint8_t)selected_month)) {
        selected_day -= schedule_month_days(selected_year, (uint8_t)selected_month);
        if (++selected_month > 12) {
            selected_month = 1;
            ++selected_year;
        }
    }
    *year = (uint16_t)selected_year;
    *month = (uint8_t)selected_month;
    *day = (uint8_t)selected_day;
}

static void schedule_refresh_async(void *user_data)
{
    (void)user_data;
    show_schedule();
}

static void schedule_swipe_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == NULL || (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED &&
                          code != LV_EVENT_PRESS_LOST)) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        s_schedule_drag_active = true;
        s_schedule_drag_start = point;
        return;
    }

    if (!s_schedule_drag_active) {
        return;
    }
    s_schedule_drag_active = false;

    const int32_t delta_x = point.x - s_schedule_drag_start.x;
    const int32_t delta_y = point.y - s_schedule_drag_start.y;
    const int32_t abs_x = delta_x < 0 ? -delta_x : delta_x;
    const int32_t abs_y = delta_y < 0 ? -delta_y : delta_y;
    if (abs_x < 24 || abs_x <= abs_y) {
        return;
    }

    const uint8_t old_day = s_schedule_day;
    if (delta_x < 0 && s_schedule_day < 4) {
        ++s_schedule_day;
    } else if (delta_x > 0 && s_schedule_day > 0) {
        --s_schedule_day;
    }
    if (s_schedule_day == old_day) {
        return;
    }
    if (app_settings_volume() >= 10) {
        alarm_audio_click();
    }
    lv_async_call(schedule_refresh_async, NULL);
}

static void show_schedule(void)
{
    s_screen = SCREEN_SCHEDULE;
    lv_obj_t *root = prepare_screen();
    s_schedule_drag_active = false;
    make_label(root, "本周课程表", FONT_CJK_BOLD, COLOR_TEXT, 9, 7, 115, 28,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, s_weekday_names[s_schedule_day], FONT_CJK_BOLD, COLOR_MATH,
               10, 41, 112, 25,
               LV_TEXT_ALIGN_CENTER);
    uint16_t selected_year;
    uint8_t selected_month;
    uint8_t selected_day;
    schedule_selected_date(&selected_year, &selected_month, &selected_day);
    (void)selected_year;
    char date_text[20];
    snprintf(date_text, sizeof(date_text), "%u月%u日", selected_month, selected_day);
    make_label(root, date_text, FONT_CJK, COLOR_MUTED, 10, 75, 112, 23,
               LV_TEXT_ALIGN_CENTER);
    make_button(root, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 18, 111, 96, 42, menu_event,
                NULL);

    for (uint8_t period = 0; period < 7; ++period) {
        const char *course = s_schedule[s_schedule_day][period];
        const uint32_t color = course_color(course);
        lv_obj_t *card = make_panel(root, 128 + period * 72, 9, 66, 153, color,
                                    course_dark_color(color));
        lv_obj_set_style_shadow_width(card, 7, 0);
        lv_obj_t *period_badge = make_shape(card, 13, 17, 40, 40, COLOR_TEXT,
                                             LV_OPA_20, LV_RADIUS_CIRCLE);
        char period_text[4];
        snprintf(period_text, sizeof(period_text), "%u", (unsigned)(period + 1));
        lv_obj_t *period_label = make_label(period_badge, period_text, FONT_VALUE, COLOR_TEXT,
                                             0, 0, 40, 24, LV_TEXT_ALIGN_CENTER);
        lv_obj_center(period_label);
        make_shape(card, 19, 72, 28, 2, COLOR_TEXT, LV_OPA_30, 1);
        make_label(card, course, FONT_CJK_BOLD, COLOR_TEXT, 3, 91, 60, 29,
                   LV_TEXT_ALIGN_CENTER);
    }

    /* A transparent interaction layer over the course cards captures a full
     * drag and changes exactly one day on release. The day tabs and Back
     * button stay outside this layer and retain normal tap behavior. */
    lv_obj_t *swipe_area = lv_obj_create(root);
    lv_obj_set_pos(swipe_area, 126, 6);
    lv_obj_set_size(swipe_area, 508, 160);
    lv_obj_remove_flag(swipe_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(swipe_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(swipe_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swipe_area, 0, 0);
    lv_obj_set_style_pad_all(swipe_area, 0, 0);
    lv_obj_add_event_cb(swipe_area, schedule_swipe_event, LV_EVENT_ALL, NULL);
}

static void wifi_scan_event(lv_event_t *event)
{
    (void)event;
    wifi_manager_scan();
    show_wifi(false);
}

static void wifi_disconnect_event(lv_event_t *event)
{
    (void)event;
    wifi_manager_disconnect();
    show_wifi(true);
}

static void wifi_network_event(lv_event_t *event)
{
    const size_t index = (size_t)(intptr_t)lv_event_get_user_data(event);
    if (index >= s_wifi_page_count) {
        return;
    }
    strlcpy(s_selected_ssid, s_wifi_page_networks[index].ssid, sizeof(s_selected_ssid));
    if (!s_wifi_page_networks[index].secured) {
        wifi_manager_connect(s_selected_ssid, "");
        show_wifi(false);
        return;
    }
    show_wifi_password();
}

static const char *wifi_state_text(wifi_manager_state_t state)
{
    switch (state) {
        case WIFI_MANAGER_SCANNING:
            return "正在扫描";
        case WIFI_MANAGER_CONNECTING:
            return "正在连接";
        case WIFI_MANAGER_CONNECTED:
            return "已连接";
        case WIFI_MANAGER_FAILED:
            return "连接失败";
        default:
            return "未连接";
    }
}

static lv_obj_t *make_wifi_network_card(lv_obj_t *parent, size_t index, int x,
                                        bool connected)
{
    const uint32_t accents[] = {COLOR_WIFI, COLOR_BREAK, COLOR_ENGLISH};
    const uint32_t accent = connected ? COLOR_BREAK : accents[index];
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_pos(card, x, 50);
    lv_obj_set_size(card, 154, 112);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL_DARK), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(card, connected ? 2 : 1, 0);
    lv_obj_set_style_shadow_width(card, connected ? 8 : 0, 0);
    lv_obj_set_style_shadow_opa(card, connected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(accent), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(card, wifi_network_event, LV_EVENT_PRESSED,
                        (void *)(intptr_t)index);

    make_label(card, LV_SYMBOL_WIFI, FONT_CLOCK, accent, 0, 13, 154, 34,
               LV_TEXT_ALIGN_CENTER);
    lv_obj_t *ssid = make_label(card, s_wifi_page_networks[index].ssid, FONT_CJK, COLOR_TEXT,
                                11, 53, 132, 24, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_MODE_DOTS);
    if (connected) {
        make_label(card, "已连接", FONT_CJK, COLOR_BREAK, 8, 83, 138, 20,
                   LV_TEXT_ALIGN_CENTER);
    } else {
        const uint8_t bars = s_wifi_page_networks[index].rssi >= -60 ? 3
                             : s_wifi_page_networks[index].rssi >= -75 ? 2
                                                                      : 1;
        const int start_x = 70 - (int)bars * 4;
        for (uint8_t bar = 0; bar < bars; ++bar) {
            const int height = 4 + bar * 3;
            make_shape(card, start_x + bar * 8, 100 - height, 5, height, accent,
                       LV_OPA_COVER, 2);
        }
    }
    return card;
}

static void show_wifi(bool start_scan)
{
    s_screen = SCREEN_WIFI;
    lv_obj_t *root = prepare_screen();
    wifi_manager_status_t status;
    wifi_manager_get_status(&status);
    s_wifi_generation = status.generation;
    s_wifi_page_count = wifi_manager_get_networks(s_wifi_page_networks,
                                                   WIFI_MANAGER_MAX_NETWORKS);

    const bool connected = status.state == WIFI_MANAGER_CONNECTED;
    lv_obj_t *rail = make_panel(root, 8, 8, 112, 156,
                                connected ? COLOR_BREAK : COLOR_WIFI,
                                COLOR_PANEL_DARK);
    make_label(rail, LV_SYMBOL_WIFI, FONT_CLOCK, connected ? COLOR_BREAK : COLOR_WIFI,
               0, 7, 112, 31, LV_TEXT_ALIGN_CENTER);
    make_label(rail, "Wi-Fi", LV_FONT_DEFAULT, COLOR_TEXT, 0, 39, 112, 20,
               LV_TEXT_ALIGN_CENTER);
    make_shape(rail, 13, 72, 8, 8, connected ? COLOR_BREAK : COLOR_MUTED,
               LV_OPA_COVER, LV_RADIUS_CIRCLE);
    make_label(rail, wifi_state_text(status.state), FONT_CJK,
               connected ? COLOR_BREAK : COLOR_MUTED, 27, 66, 79, 20,
               LV_TEXT_ALIGN_LEFT);
    lv_obj_t *current_ssid = make_label(rail, status.ssid[0] != '\0' ? status.ssid : "--",
                                        FONT_CJK, COLOR_TEXT, 8, 90, 96, 20,
                                        LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(current_ssid, LV_LABEL_LONG_MODE_DOTS);
    if (connected) {
        make_button(rail, "断开", COLOR_DANGER, COLOR_DANGER_2, 5, 119, 49, 31,
                    wifi_disconnect_event, NULL);
        make_button(rail, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 58, 119, 49, 31,
                    menu_event, NULL);
    } else {
        make_button(rail, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 6, 119, 100, 31,
                    menu_event, NULL);
    }

    make_label(root, "选择网络", FONT_CJK_BOLD, COLOR_TEXT, 136, 10, 220, 29,
               LV_TEXT_ALIGN_LEFT);
    make_button(root, "扫描", COLOR_CHINESE, COLOR_CHINESE_2, 520, 7, 110, 35,
                wifi_scan_event, NULL);

    if (s_wifi_page_count == 0) {
        make_label(root, status.state == WIFI_MANAGER_SCANNING ? "正在扫描，请稍候"
                                                               : "没有发现可用网络",
                   FONT_CJK, COLOR_MUTED, 136, 91, 494, 28, LV_TEXT_ALIGN_CENTER);
    }
    const size_t visible = s_wifi_page_count > 3 ? 3 : s_wifi_page_count;
    for (size_t index = 0; index < visible; ++index) {
        const bool is_current = connected &&
                                strcmp(status.ssid, s_wifi_page_networks[index].ssid) == 0;
        make_wifi_network_card(root, index, 136 + (int)index * 164, is_current);
    }
    if (start_scan && status.state != WIFI_MANAGER_SCANNING &&
        status.state != WIFI_MANAGER_CONNECTING) {
        wifi_manager_scan();
    }
}

static void wifi_connect_from_keyboard(void)
{
    if (s_password_textarea == NULL) {
        return;
    }
    const char *password = lv_textarea_get_text(s_password_textarea);
    const esp_err_t err = wifi_manager_connect(s_selected_ssid, password);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi connect request failed: %s", esp_err_to_name(err));
    }
    show_wifi(false);
}

static void wifi_keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        wifi_connect_from_keyboard();
    } else if (code == LV_EVENT_CANCEL) {
        show_wifi(false);
    }
}

static void wifi_connect_button_event(lv_event_t *event)
{
    (void)event;
    wifi_connect_from_keyboard();
}

static void show_wifi_password(void)
{
    s_screen = SCREEN_WIFI_PASSWORD;
    lv_obj_t *root = prepare_screen();
    make_label(root, "输入 Wi-Fi 密码", FONT_CJK_BOLD, COLOR_TEXT, 4, 5, 138, 27,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, s_selected_ssid, LV_FONT_DEFAULT, COLOR_MUTED, 5, 31, 136, 20,
               LV_TEXT_ALIGN_CENTER);

    s_password_textarea = lv_textarea_create(root);
    lv_obj_set_pos(s_password_textarea, 6, 55);
    lv_obj_set_size(s_password_textarea, 136, 43);
    lv_textarea_set_one_line(s_password_textarea, true);
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_textarea_set_max_length(s_password_textarea, 63);
    lv_textarea_set_placeholder_text(s_password_textarea, "请输入密码");
    lv_obj_set_style_bg_color(s_password_textarea, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(s_password_textarea, lv_color_hex(COLOR_CHINESE), 0);
    lv_obj_set_style_border_width(s_password_textarea, 2, 0);
    lv_obj_set_style_text_color(s_password_textarea, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_password_textarea, FONT_CJK, 0);

    make_button(root, "取消", COLOR_PANEL, COLOR_PANEL_DARK, 6, 109, 64, 51,
                 wifi_event, NULL);
    make_button(root, "连接", COLOR_CHINESE, COLOR_CHINESE_2, 77, 109, 65, 51,
                 wifi_connect_button_event, NULL);

    lv_obj_t *keyboard = lv_keyboard_create(root);
    /* lv_keyboard_create() applies LV_ALIGN_BOTTOM_MID by default. Coordinates
     * passed to set_pos() were therefore offsets from center, which pushed the
     * matrix past the right edge. Reset alignment before absolute positioning. */
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 150, 4);
    lv_obj_set_size(keyboard, 480, 164);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, s_keyboard_lower_map,
                        s_keyboard_ctrl_map);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, s_keyboard_upper_map,
                        s_keyboard_ctrl_map);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, s_keyboard_special_map,
                        s_keyboard_special_ctrl_map);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, s_password_textarea);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_PANEL_DARK), 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 2, 0);
    lv_obj_set_style_pad_row(keyboard, 2, 0);
    lv_obj_set_style_pad_column(keyboard, 2, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_CHINESE_2), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 7, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(COLOR_CHINESE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_CHINESE),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(COLOR_TEXT),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_CHINESE),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(keyboard, touch_sound_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(keyboard, wifi_keyboard_event, LV_EVENT_ALL, NULL);
}

static void alarm_refresh_async(void *user_data)
{
    (void)user_data;
    show_alarm();
}

static void save_alarm_and_refresh(const alarm_config_t *config)
{
    const esp_err_t err = alarm_store_save(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save alarm failed: %s", esp_err_to_name(err));
    }
    lv_async_call(alarm_refresh_async, NULL);
}

static void alarm_adjust_event(lv_event_t *event)
{
    alarm_config_t config = *alarm_store_get();
    switch ((alarm_adjust_t)(intptr_t)lv_event_get_user_data(event)) {
        case ALARM_HOUR_DOWN:
            config.hour = config.hour == 0 ? 23 : (uint8_t)(config.hour - 1);
            break;
        case ALARM_HOUR_UP:
            config.hour = (uint8_t)((config.hour + 1) % 24);
            break;
        case ALARM_MINUTE_DOWN:
            config.minute = (uint8_t)((config.minute + 55) % 60);
            break;
        case ALARM_MINUTE_UP:
            config.minute = (uint8_t)((config.minute + 5) % 60);
            break;
    }
    save_alarm_and_refresh(&config);
}

static void alarm_switch_event(lv_event_t *event)
{
    lv_obj_t *alarm_switch = lv_event_get_target_obj(event);
    alarm_config_t config = *alarm_store_get();
    const bool enabled = !config.enabled;

    /* Use the same first-press + stable-release gate as the sound switch.
     * Rebuilding this page from VALUE_CHANGED left a fresh switch under a
     * noisy second touch report, which immediately saved the opposite state. */
    board_display_block_touch_until_release();
    config.enabled = enabled;
    const esp_err_t err = alarm_store_save(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save alarm enabled state failed: %s", esp_err_to_name(err));
        return;
    }
    if (enabled) {
        lv_obj_add_state(alarm_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(alarm_switch, LV_STATE_CHECKED);
    }
    if (s_alarm_status_label != NULL) {
        lv_label_set_text(s_alarm_status_label, enabled ? "闹钟已开启" : "闹钟已关闭");
        lv_obj_set_style_text_color(s_alarm_status_label,
                                    lv_color_hex(enabled ? COLOR_BREAK : COLOR_MUTED), 0);
    }
}

static void alarm_repeat_event(lv_event_t *event)
{
    (void)event;
    alarm_config_t config = *alarm_store_get();
    config.days_mask = config.days_mask == 0x7F ? 0x1F : 0x7F;
    save_alarm_and_refresh(&config);
}

static void show_alarm(void)
{
    s_screen = SCREEN_ALARM;
    lv_obj_t *root = prepare_screen();
    const alarm_config_t *config = alarm_store_get();

    make_label(root, "学习闹钟", FONT_CJK_BOLD, COLOR_TEXT, 12, 14, 113, 29,
               LV_TEXT_ALIGN_CENTER);
    s_alarm_status_label = make_label(root,
                                      config->enabled ? "闹钟已开启" : "闹钟已关闭",
                                      FONT_CJK,
                                      config->enabled ? COLOR_BREAK : COLOR_MUTED,
                                      8, 51, 121, 23, LV_TEXT_ALIGN_CENTER);
    make_label(root, alarm_audio_available() ? "板载扬声器响铃" : "屏幕提醒模式", FONT_CJK,
               COLOR_MUTED, 8, 83, 121, 22, LV_TEXT_ALIGN_CENTER);
    make_button(root, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 17, 119, 103, 39, menu_event,
                NULL);

    char hour[4];
    char minute[4];
    snprintf(hour, sizeof(hour), "%02u", config->hour);
    snprintf(minute, sizeof(minute), "%02u", config->minute);
    make_button(root, "+", COLOR_CHINESE, COLOR_CHINESE_2, 148, 10, 72, 34,
                alarm_adjust_event, (void *)(intptr_t)ALARM_HOUR_UP);
    make_label(root, hour, FONT_TIMER, COLOR_TEXT, 137, 56, 94, 58, LV_TEXT_ALIGN_CENTER);
    make_button(root, "-", COLOR_PANEL, COLOR_PANEL_DARK, 148, 122, 72, 34,
                alarm_adjust_event, (void *)(intptr_t)ALARM_HOUR_DOWN);
    make_label(root, ":", FONT_CLOCK, COLOR_TEXT, 224, 63, 30, 44, LV_TEXT_ALIGN_CENTER);
    make_button(root, "+", COLOR_CHINESE, COLOR_CHINESE_2, 258, 10, 72, 34,
                alarm_adjust_event, (void *)(intptr_t)ALARM_MINUTE_UP);
    make_label(root, minute, FONT_TIMER, COLOR_TEXT, 247, 56, 94, 58, LV_TEXT_ALIGN_CENTER);
    make_button(root, "-", COLOR_PANEL, COLOR_PANEL_DARK, 258, 122, 72, 34,
                alarm_adjust_event, (void *)(intptr_t)ALARM_MINUTE_DOWN);

    lv_obj_t *settings = make_panel(root, 362, 16, 264, 140, COLOR_ENGLISH, COLOR_ENGLISH_2);
    make_label(settings, "启用闹钟", FONT_CJK, COLOR_TEXT, 12, 14, 115, 25,
               LV_TEXT_ALIGN_LEFT);
    s_alarm_switch = lv_switch_create(settings);
    lv_obj_set_pos(s_alarm_switch, 187, 10);
    lv_obj_set_size(s_alarm_switch, 56, 30);
    if (config->enabled) {
        lv_obj_add_state(s_alarm_switch, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(s_alarm_switch, lv_color_hex(COLOR_BREAK),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_alarm_switch, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_alarm_switch, alarm_switch_event, LV_EVENT_PRESSED, NULL);

    make_label(settings, "重复", FONT_CJK, COLOR_MUTED, 12, 61, 60, 23, LV_TEXT_ALIGN_LEFT);
    make_button(settings, config->days_mask == 0x7F ? "每天" : "工作日", COLOR_CHINESE,
                COLOR_CHINESE_2, 89, 53, 154, 39, alarm_repeat_event, NULL);
    make_label(settings, "分钟按 5 分钟调整", FONT_CJK, COLOR_MUTED, 12, 108, 231, 22,
               LV_TEXT_ALIGN_CENTER);
}

static void set_display_sleeping(bool sleeping)
{
    if (sleeping == s_display_sleeping) {
        return;
    }
    const esp_err_t wifi_error = wifi_manager_set_low_power(sleeping);
    if (wifi_error != ESP_OK) {
        ESP_LOGW(TAG, "set Wi-Fi power saving failed: %s", esp_err_to_name(wifi_error));
    }
    const esp_err_t display_error = board_display_set_sleeping(sleeping);
    if (display_error != ESP_OK) {
        ESP_LOGW(TAG, "set display sleep state failed: %s", esp_err_to_name(display_error));
    }
    s_display_sleeping = sleeping;
    if (!sleeping) {
        lv_display_trigger_activity(NULL);
    }
}

static void update_lock_screen_status(void)
{
    if (s_lock_clock_label != NULL) {
        char clock_text[8];
        snprintf(clock_text, sizeof(clock_text), "%02u:%02u", s_now.hour, s_now.minute);
        lv_label_set_text(s_lock_clock_label, clock_text);
    }
    if (s_lock_status_label != NULL) {
        char status_text[64];
        if (s_session.active) {
            char elapsed[16];
            format_timer(session_elapsed_seconds(), elapsed, sizeof(elapsed));
            snprintf(status_text, sizeof(status_text), "%s%s  %s",
                     s_subject_names[s_session.subject], s_session.running ? "计时中" : "已暂停",
                     elapsed);
        } else {
            snprintf(status_text, sizeof(status_text), "已锁屏 · 闹钟正常待命");
        }
        lv_label_set_text(s_lock_status_label, status_text);
    }
}

static void restore_lock_return_screen(void)
{
    switch (s_lock_return_screen) {
        case SCREEN_WEATHER:
            show_weather();
            break;
        case SCREEN_TIMER:
            if (s_session.active) {
                show_timer();
            } else {
                show_home();
            }
            break;
        case SCREEN_REPORT:
            show_report();
            break;
        case SCREEN_MENU:
            show_menu();
            break;
        case SCREEN_SETTINGS:
            show_settings();
            break;
        case SCREEN_FOCUS_SETTINGS:
            show_focus_settings();
            break;
        case SCREEN_SCHEDULE:
            show_schedule();
            break;
        case SCREEN_WIFI:
        case SCREEN_WIFI_PASSWORD:
            show_wifi(false);
            break;
        case SCREEN_ALARM:
            show_alarm();
            break;
        default:
            show_home();
            break;
    }
}

static void unlock_screen_event(lv_event_t *event)
{
    if (!s_locked || s_display_sleeping) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_LONG_PRESSED) {
        /* Do not replace the screen while the finger is still down. Otherwise
         * the same physical press can land on a button on the restored page. */
        s_unlock_long_press_confirmed = true;
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        s_unlock_long_press_confirmed = false;
        return;
    }
    if (code != LV_EVENT_RELEASED || !s_unlock_long_press_confirmed) {
        return;
    }

    s_unlock_long_press_confirmed = false;
    if (app_settings_volume() >= 10) {
        alarm_audio_click();
    }
    board_display_block_touch_until_release();
    s_locked = false;
    lv_display_trigger_activity(NULL);
    restore_lock_return_screen();
}

static void show_lock_screen(void)
{
    s_unlock_long_press_confirmed = false;
    s_screen = SCREEN_LOCK;
    lv_obj_t *root = prepare_screen();

    lv_obj_t *lock_badge = make_panel(root, 8, 8, 128, 156, COLOR_WIFI, COLOR_WIFI_2);
    add_image_icon(lock_badge, &ui_icon_settings_lock, 34, 18);
    make_label(lock_badge, "睡眠锁屏", FONT_CJK_BOLD, COLOR_TEXT, 6, 103, 116, 30,
               LV_TEXT_ALIGN_CENTER);

    s_lock_clock_label = make_label(root, "--:--", FONT_LOCK_CLOCK, COLOR_TEXT,
                                     146, 38, 348, 72,
                                     LV_TEXT_ALIGN_CENTER);
    s_lock_status_label = make_label(root, "已锁屏", FONT_CJK, COLOR_BREAK,
                                     146, 118, 348, 28,
                                      LV_TEXT_ALIGN_CENTER);

    lv_obj_t *unlock = lv_button_create(root);
    lv_obj_set_pos(unlock, 504, 8);
    lv_obj_set_size(unlock, 128, 156);
    style_surface(unlock, COLOR_CHINESE, COLOR_CHINESE_2, 15, 2);
    lv_obj_add_event_cb(unlock, unlock_screen_event, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(unlock, unlock_screen_event, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(unlock, unlock_screen_event, LV_EVENT_PRESS_LOST, NULL);
    add_image_icon(unlock, &ui_icon_lock_unlock, 34, 18);
    make_label(unlock, "长按解锁", FONT_CJK_BOLD, COLOR_TEXT, 6, 103, 116, 30,
               LV_TEXT_ALIGN_CENTER);
    update_lock_screen_status();
}

static void enter_lock_mode(void)
{
    if (s_locked || s_screen == SCREEN_ALARM_RING) {
        return;
    }
    s_lock_return_screen = s_screen;
    s_locked = true;
    show_lock_screen();
    lv_refr_now(NULL);
    set_display_sleeping(true);
    ESP_LOGI(TAG, "screen locked");
}

static void wake_lock_screen(void)
{
    if (!s_locked || !s_display_sleeping) {
        return;
    }
    show_lock_screen();
    lv_refr_now(NULL);
    set_display_sleeping(false);
    s_lock_awake_deadline_ms = uptime_ms() + 15000;
    ESP_LOGI(TAG, "lock screen awake");
}

static void stop_alarm_event(lv_event_t *event)
{
    (void)event;
    alarm_audio_stop();
    s_snooze_active = false;
    if (s_alarm_return_screen == SCREEN_TIMER && s_session.active) {
        show_timer();
    } else {
        show_home();
    }
}

static void snooze_alarm_event(lv_event_t *event)
{
    (void)event;
    alarm_audio_stop();
    s_snooze_active = true;
    s_snooze_due_ms = uptime_ms() + 5 * 60 * 1000;
    if (s_alarm_return_screen == SCREEN_TIMER && s_session.active) {
        show_timer();
    } else {
        show_home();
    }
}

static void show_alarm_ring(void)
{
    s_screen = SCREEN_ALARM_RING;
    lv_obj_t *root = prepare_screen();
    lv_obj_t *bell = make_panel(root, 22, 27, 115, 118, COLOR_MATH, COLOR_MATH_2);
    make_label(bell, "!", FONT_TIMER, COLOR_TEXT, 10, 10, 95, 58, LV_TEXT_ALIGN_CENTER);
    make_label(bell, "闹钟响铃", FONT_CJK_BOLD, COLOR_TEXT, 5, 76, 105, 28,
               LV_TEXT_ALIGN_CENTER);

    char time_text[8];
    snprintf(time_text, sizeof(time_text), "%02u:%02u", s_now.hour, s_now.minute);
    make_label(root, time_text, FONT_TIMER, COLOR_TEXT, 158, 28, 236, 63,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, "时间到了，准备开始新一天!", FONT_CJK, COLOR_BREAK, 146, 102, 261, 27,
               LV_TEXT_ALIGN_CENTER);
    make_button(root, "稍后 5 分钟", COLOR_PANEL, COLOR_PANEL_DARK, 430, 24, 190, 55,
                snooze_alarm_event, NULL);
    make_button(root, "关闭闹钟", COLOR_CHINESE, COLOR_CHINESE_2, 430, 92, 190, 55,
                stop_alarm_event, NULL);
}

static void trigger_alarm(void)
{
    if (s_screen == SCREEN_ALARM_RING) {
        return;
    }
    s_alarm_return_screen = s_locked ? s_lock_return_screen : s_screen;
    if (s_locked) {
        s_locked = false;
        set_display_sleeping(false);
        ESP_LOGI(TAG, "alarm woke the locked display");
    }
    const esp_err_t err = alarm_audio_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "alarm audio start failed: %s", esp_err_to_name(err));
    }
    show_alarm_ring();
}

static void ui_tick(lv_timer_t *timer)
{
    (void)timer;
    const int64_t now_ms = uptime_ms();

    if (board_power_take_short_press()) {
        if (s_screen != SCREEN_ALARM_RING) {
            if (!s_locked) {
                enter_lock_mode();
            } else if (s_display_sleeping) {
                wake_lock_screen();
            } else {
                set_display_sleeping(true);
            }
        }
    }

    if (s_screen == SCREEN_TIMER && s_session.active) {
        const uint32_t elapsed = session_elapsed_seconds();
        if (elapsed != s_last_rendered_second) {
            char time_text[16];
            format_timer(elapsed, time_text, sizeof(time_text));
            if (s_timer_echo_label != NULL) {
                lv_label_set_text(s_timer_echo_label, time_text);
            }
            lv_label_set_text(s_timer_label, time_text);
            lv_bar_set_value(s_progress_bar,
                             (int32_t)(elapsed > s_timer_target_seconds
                                           ? s_timer_target_seconds
                                           : elapsed),
                             LV_ANIM_OFF);
            s_last_rendered_second = elapsed;
        }
    } else if (s_screen == SCREEN_LOCK && s_session.active && !s_display_sleeping) {
        const uint32_t elapsed = session_elapsed_seconds();
        if (elapsed != s_last_rendered_second) {
            update_lock_screen_status();
            s_last_rendered_second = elapsed;
        }
    }

    if (s_screen == SCREEN_WIFI) {
        wifi_manager_status_t status;
        wifi_manager_get_status(&status);
        if (status.generation != s_wifi_generation) {
            show_wifi(false);
        }
    }

    if (s_screen == SCREEN_WEATHER) {
        update_weather_page(false);
    }

    if (s_snooze_active && now_ms >= s_snooze_due_ms) {
        s_snooze_active = false;
        trigger_alarm();
    }

    if (now_ms - s_last_rtc_poll_ms >= 1000) {
        s_last_rtc_poll_ms = now_ms;
        update_home_status_icons(false);
        if (rtc_pcf85063_read(&s_now)) {
            if (s_screen == SCREEN_LOCK) {
                update_lock_screen_status();
            }
            if (s_screen == SCREEN_HOME && s_clock_label != NULL &&
                s_now.minute != s_last_clock_minute) {
                char clock_text[8];
                snprintf(clock_text, sizeof(clock_text), "%02u:%02u", s_now.hour, s_now.minute);
                lv_label_set_text(s_clock_label, clock_text);
                s_last_clock_minute = s_now.minute;
            }
            const int32_t civil_day = study_days_from_civil(s_now.year, s_now.month, s_now.day);
            const int64_t alarm_key = (int64_t)civil_day * 1440 + s_now.hour * 60 + s_now.minute;
            if (alarm_key != s_last_alarm_key && alarm_store_matches(&s_now)) {
                s_last_alarm_key = alarm_key;
                trigger_alarm();
            }
        }
    }

    if (s_locked && !s_display_sleeping && now_ms >= s_lock_awake_deadline_ms) {
        set_display_sleeping(true);
    } else if (!s_locked && s_screen != SCREEN_ALARM_RING) {
        const uint8_t auto_lock_minutes = app_settings_auto_lock_minutes();
        if (auto_lock_minutes > 0 &&
            lv_display_get_inactive_time(NULL) >= (uint32_t)auto_lock_minutes * 60000U) {
            enter_lock_mode();
        }
    }
}

esp_err_t study_ui_start(void)
{
    if (!rtc_pcf85063_read(&s_now)) {
        return ESP_ERR_INVALID_STATE;
    }
    show_home();
    if (lv_timer_create(ui_tick, 250, NULL) == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "study UI started");
    return ESP_OK;
}
