#include "study_ui.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alarm_audio.h"
#include "alarm_store.h"
#include "app_settings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "rtc_pcf85063.h"
#include "study_store.h"
#include "wifi_manager.h"

LV_FONT_DECLARE(ui_font_16);

#define FONT_CJK (&ui_font_16)

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

typedef enum {
    SCREEN_HOME,
    SCREEN_TIMER,
    SCREEN_REPORT,
    SCREEN_MENU,
    SCREEN_SETTINGS,
    SCREEN_SCHEDULE,
    SCREEN_WIFI,
    SCREEN_WIFI_PASSWORD,
    SCREEN_ALARM,
    SCREEN_ALARM_RING,
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
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "z", "x", "c", "v", "b", "n", "m", ".", "-", "\n",
    "ABC", " ", LV_SYMBOL_OK, "",
};

static const char *const s_keyboard_upper_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "Z", "X", "C", "V", "B", "N", "M", ".", "-", "\n",
    "abc", " ", LV_SYMBOL_OK, "",
};

static const lv_buttonmatrix_ctrl_t s_keyboard_ctrl_map[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, 8, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
};

static active_session_t s_session;
static screen_id_t s_screen;
static rtc_datetime_t s_now;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_timer_status_label;
static lv_obj_t *s_pause_label;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_password_textarea;
static lv_obj_t *s_alarm_switch;
static lv_obj_t *s_volume_label;
static uint32_t s_last_rendered_second = UINT32_MAX;
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

static void show_home(void);
static void show_timer(void);
static void show_report(void);
static void show_menu(void);
static void show_settings(void);
static void show_schedule(void);
static void show_wifi(bool start_scan);
static void show_wifi_password(void);
static void show_alarm(void);
static void show_alarm_ring(void);

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

static void format_hms(uint32_t seconds, char *buffer, size_t length)
{
    snprintf(buffer, length, "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
             seconds / 3600U, (seconds / 60U) % 60U, seconds % 60U);
}

static void format_minutes(uint32_t seconds, char *buffer, size_t length)
{
    const uint32_t minutes = (seconds + 30U) / 60U;
    if (minutes < 60U) {
        snprintf(buffer, length, "%" PRIu32 "分", minutes);
    } else {
        snprintf(buffer, length, "%" PRIu32 "时%02" PRIu32 "分", minutes / 60U,
                 minutes % 60U);
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, int x, int y, int width, int height,
                            lv_text_align_t alignment)
{
    lv_obj_t *label = lv_label_create(parent);
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
    if (app_settings_button_sound_enabled()) {
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
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, width - 12);
    lv_obj_set_style_text_font(label, FONT_CJK, 0);
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

static void add_book_icon(lv_obj_t *card)
{
    make_shape(card, 20, 15, 32, 39, 0xEAF4FF, LV_OPA_COVER, 5);
    make_shape(card, 56, 15, 32, 39, 0xEAF4FF, LV_OPA_COVER, 5);
    make_shape(card, 51, 18, 6, 39, 0x82B8FF, LV_OPA_COVER, 2);
    make_shape(card, 27, 25, 18, 2, COLOR_CHINESE, LV_OPA_50, 1);
    make_shape(card, 63, 25, 18, 2, COLOR_CHINESE, LV_OPA_50, 1);
    make_shape(card, 27, 34, 18, 2, COLOR_CHINESE, LV_OPA_50, 1);
    make_shape(card, 63, 34, 18, 2, COLOR_CHINESE, LV_OPA_50, 1);
}

static void add_compass_icon(lv_obj_t *card)
{
    static const lv_point_precise_t left_leg[] = {{13, 0}, {0, 29}};
    static const lv_point_precise_t right_leg[] = {{0, 0}, {13, 29}};

    lv_obj_t *left = lv_line_create(card);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_CLICKABLE);
    lv_line_set_points(left, left_leg, 2);
    lv_obj_set_pos(left, 33, 34);
    lv_obj_set_style_line_width(left, 7, 0);
    lv_obj_set_style_line_rounded(left, true, 0);
    lv_obj_set_style_line_color(left, lv_color_hex(0xFFE071), 0);

    lv_obj_t *right = lv_line_create(card);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_CLICKABLE);
    lv_line_set_points(right, right_leg, 2);
    lv_obj_set_pos(right, 62, 34);
    lv_obj_set_style_line_width(right, 7, 0);
    lv_obj_set_style_line_rounded(right, true, 0);
    lv_obj_set_style_line_color(right, lv_color_hex(0xFFE071), 0);

    make_shape(card, 39, 10, 31, 31, 0xFFE071, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    make_shape(card, 47, 18, 15, 15, COLOR_MATH_2, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    make_shape(card, 51, 22, 7, 7, 0xFFF2B2, LV_OPA_COVER, LV_RADIUS_CIRCLE);
}

static void add_english_icon(lv_obj_t *card)
{
    lv_obj_t *tile_a = make_shape(card, 39, 10, 31, 29, 0x9B8BFF, LV_OPA_COVER, 6);
    lv_obj_t *tile_b = make_shape(card, 25, 37, 31, 29, 0x6E5CE7, LV_OPA_COVER, 6);
    lv_obj_t *tile_c = make_shape(card, 54, 37, 31, 29, 0x7765EF, LV_OPA_COVER, 6);
    make_label(tile_a, "A", FONT_VALUE, COLOR_TEXT, 0, 2, 31, 24, LV_TEXT_ALIGN_CENTER);
    make_label(tile_b, "B", FONT_VALUE, COLOR_TEXT, 0, 2, 31, 24, LV_TEXT_ALIGN_CENTER);
    make_label(tile_c, "C", FONT_VALUE, COLOR_TEXT, 0, 2, 31, 24, LV_TEXT_ALIGN_CENTER);
}

static void add_cup_icon(lv_obj_t *card)
{
    lv_obj_t *handle = lv_obj_create(card);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(handle, 38, 27);
    lv_obj_set_size(handle, 23, 23);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(handle, 5, 0);
    lv_obj_set_style_border_color(handle, lv_color_hex(0xA5F1DF), 0);
    lv_obj_set_style_pad_all(handle, 0, 0);

    make_shape(card, 10, 22, 39, 34, 0xA5F1DF, LV_OPA_COVER, 8);
    make_shape(card, 16, 16, 3, 8, 0xD9FFF6, LV_OPA_70, 2);
    make_shape(card, 27, 12, 3, 11, 0xD9FFF6, LV_OPA_70, 2);
    make_shape(card, 38, 16, 3, 8, 0xD9FFF6, LV_OPA_70, 2);
}

static lv_obj_t *make_subject_card(lv_obj_t *parent, uint8_t subject, int x,
                                   uint32_t weekly_minutes, lv_event_cb_t callback)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_pos(card, x, 10);
    lv_obj_set_size(card, 110, 152);
    style_surface(card, s_subject_colors[subject], s_subject_dark_colors[subject], 14, 2);
    lv_obj_add_event_cb(card, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(card, callback, LV_EVENT_PRESSED, (void *)(intptr_t)subject);

    if (subject == STUDY_SUBJECT_CHINESE) {
        add_book_icon(card);
    } else if (subject == STUDY_SUBJECT_MATH) {
        add_compass_icon(card);
    } else {
        add_english_icon(card);
    }

    make_label(card, s_subject_names[subject], FONT_CJK, COLOR_TEXT, 0, 78, 110, 22,
               LV_TEXT_ALIGN_CENTER);
    char weekly[32];
    snprintf(weekly, sizeof(weekly), "本周 %" PRIu32 " 分", weekly_minutes);
    make_label(card, weekly, FONT_CJK, COLOR_TEXT, 0, 112, 110, 23, LV_TEXT_ALIGN_CENTER);
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
    s_timer_status_label = NULL;
    s_pause_label = NULL;
    s_progress_bar = NULL;
    s_password_textarea = NULL;
    s_alarm_switch = NULL;
    s_volume_label = NULL;
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

static void show_home(void)
{
    if (rtc_pcf85063_read(&s_now)) {
        study_store_refresh_period(&s_now);
    }
    const study_week_t *week = study_store_current();
    s_screen = SCREEN_HOME;
    lv_obj_t *root = prepare_screen();

    make_label(root, "开始写作业", FONT_CJK, COLOR_TEXT, 8, 10, 135, 24,
               LV_TEXT_ALIGN_CENTER);
    s_clock_label = make_label(root, "--:--", FONT_CLOCK, COLOR_TEXT, 8, 37, 135, 38,
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
    make_label(root, status, FONT_CJK, COLOR_BREAK, 8, 119, 135, 24, LV_TEXT_ALIGN_CENTER);

    static const int subject_x[] = {150, 267, 384};
    for (uint8_t index = 0; index < 3; ++index) {
        make_subject_card(root, index, subject_x[index],
                          (week->subject_seconds[index] + 30U) / 60U,
                          start_subject_event);
    }

    lv_obj_t *break_card = lv_button_create(root);
    lv_obj_set_pos(break_card, 501, 10);
    lv_obj_set_size(break_card, 68, 152);
    style_surface(break_card, COLOR_BREAK, COLOR_BREAK_2, 14, 2);
    lv_obj_add_event_cb(break_card, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(break_card, start_subject_event, LV_EVENT_PRESSED,
                        (void *)(intptr_t)STUDY_SUBJECT_BREAK);
    add_cup_icon(break_card);
    make_label(break_card, "休息", FONT_CJK, COLOR_TEXT, 0, 79, 68, 22,
               LV_TEXT_ALIGN_CENTER);
    char break_minutes[24];
    snprintf(break_minutes, sizeof(break_minutes), "%" PRIu32 " 分",
             (week->break_seconds + 30U) / 60U);
    make_label(break_card, break_minutes, FONT_CJK, COLOR_TEXT, 0, 113, 68, 22,
               LV_TEXT_ALIGN_CENTER);

    lv_obj_t *more_card = lv_button_create(root);
    lv_obj_set_pos(more_card, 576, 10);
    lv_obj_set_size(more_card, 56, 152);
    style_surface(more_card, COLOR_CHINESE, COLOR_PANEL_DARK, 14, 1);
    lv_obj_add_event_cb(more_card, touch_sound_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(more_card, menu_event, LV_EVENT_PRESSED, NULL);
    make_label(more_card, LV_SYMBOL_SETTINGS, FONT_CLOCK, COLOR_MUTED, 0, 32, 56, 40,
               LV_TEXT_ALIGN_CENTER);
    make_label(more_card, "更多", FONT_CJK, COLOR_TEXT, 0, 91, 56, 23,
               LV_TEXT_ALIGN_CENTER);
    s_last_clock_minute = UINT8_MAX;
}

static void show_timer(void)
{
    s_screen = SCREEN_TIMER;
    lv_obj_t *root = prepare_screen();
    const uint32_t accent = s_subject_colors[s_session.subject];
    const uint32_t dark = s_subject_dark_colors[s_session.subject];
    lv_obj_t *subject_panel = make_panel(root, 10, 25, 113, 120, accent, dark);
    make_label(subject_panel,
               s_session.subject == STUDY_SUBJECT_BREAK ? "休息一下"
                                                        : s_subject_names[s_session.subject],
               FONT_CJK, COLOR_TEXT, 2, 17, 109, 28, LV_TEXT_ALIGN_CENTER);
    s_timer_status_label = make_label(subject_panel, "正在计时", FONT_CJK, COLOR_MUTED, 2, 51,
                                      109, 25, LV_TEXT_ALIGN_CENTER);

    const study_week_t *week = study_store_current();
    char weekly[40];
    if (s_session.subject == STUDY_SUBJECT_BREAK) {
        snprintf(weekly, sizeof(weekly), "本周休息 %" PRIu32 "分",
                 (week->break_seconds + 30U) / 60U);
    } else {
        const uint8_t index = (uint8_t)s_session.subject;
        snprintf(weekly, sizeof(weekly), "今天第%u段", (unsigned)(week->sessions[index] + 1));
    }
    make_label(subject_panel, weekly, FONT_CJK, COLOR_MUTED, 2, 88, 109, 23,
               LV_TEXT_ALIGN_CENTER);

    s_timer_label = make_label(root, "00:00:00", FONT_TIMER, COLOR_TEXT, 135, 51, 252, 63,
                               LV_TEXT_ALIGN_CENTER);
    s_progress_bar = lv_bar_create(root);
    lv_obj_set_pos(s_progress_bar, 151, 132);
    lv_obj_set_size(s_progress_bar, 222, 10);
    lv_obj_set_style_radius(s_progress_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, 5, LV_PART_INDICATOR);
    lv_bar_set_range(s_progress_bar, 0, 3600);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(accent), LV_PART_INDICATOR);

    lv_obj_t *pause_button = make_button(root, "暂停", COLOR_DANGER, COLOR_DANGER_2, 405, 27,
                                         98, 118, pause_event, NULL);
    s_pause_label = lv_obj_get_child(pause_button, 0);
    make_button(root, s_session.subject == STUDY_SUBJECT_BREAK ? "结束休息" : "完成本科",
                COLOR_CHINESE, COLOR_CHINESE_2, 514, 27, 116, 118, finish_event, NULL);
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
    make_label(root, "本周学习报告", FONT_CJK, COLOR_TEXT, 8, 14, 135, 28,
               LV_TEXT_ALIGN_CENTER);

    char total[40];
    format_minutes(study_store_focus_seconds(), total, sizeof(total));
    make_label(root, "专注", FONT_CJK, COLOR_MUTED, 15, 52, 45, 22, LV_TEXT_ALIGN_LEFT);
    make_label(root, total, FONT_CJK, COLOR_BREAK, 55, 49, 88, 25, LV_TEXT_ALIGN_CENTER);
    char rest[40];
    format_minutes(week->break_seconds, rest, sizeof(rest));
    make_label(root, "休息", FONT_CJK, COLOR_MUTED, 15, 94, 45, 22, LV_TEXT_ALIGN_LEFT);
    make_label(root, rest, FONT_CJK, COLOR_BREAK, 55, 93, 88, 24, LV_TEXT_ALIGN_CENTER);

    static const int card_x[] = {151, 285, 419};
    for (uint8_t index = 0; index < 3; ++index) {
        lv_obj_t *card = make_panel(root, card_x[index], 16, 124, 140, s_subject_colors[index],
                                    s_subject_dark_colors[index]);
        make_label(card, s_subject_names[index], FONT_CJK, COLOR_TEXT, 4, 10, 112, 25,
                   LV_TEXT_ALIGN_CENTER);
        char value[40];
        format_minutes(week->subject_seconds[index], value, sizeof(value));
        make_label(card, value, FONT_VALUE, COLOR_TEXT, 3, 48, 114, 31, LV_TEXT_ALIGN_CENTER);
        char sessions[32];
        snprintf(sessions, sizeof(sessions), "%u段", week->sessions[index]);
        make_label(card, sessions, FONT_CJK, COLOR_MUTED, 4, 94, 112, 24,
                   LV_TEXT_ALIGN_CENTER);
    }
    make_button(root, "返回", COLOR_CHINESE, COLOR_CHINESE_2, 557, 45, 73, 83, menu_event,
                NULL);
}

static void show_menu(void)
{
    s_screen = SCREEN_MENU;
    lv_obj_t *root = prepare_screen();
    make_label(root, "功能菜单", FONT_CJK, COLOR_TEXT, 10, 23, 112, 26, LV_TEXT_ALIGN_CENTER);
    make_label(root, "选择要使用的功能", FONT_CJK, COLOR_MUTED, 8, 59, 116, 25,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, "学习与生活小助手", FONT_CJK, COLOR_BREAK, 8, 109, 116, 25,
               LV_TEXT_ALIGN_CENTER);

    make_button(root, "周报", COLOR_CHINESE, COLOR_CHINESE_2, 128, 18, 78, 136,
                report_event, NULL);
    make_button(root, "课程表", COLOR_MATH, COLOR_MATH_2, 213, 18, 78, 136,
                schedule_event, NULL);
    make_button(root, "闹钟", COLOR_ENGLISH, COLOR_ENGLISH_2, 298, 18, 78, 136, alarm_event,
                NULL);
    make_button(root, "Wi-Fi", COLOR_WIFI, COLOR_WIFI_2, 383, 18, 78, 136, wifi_event,
                NULL);
    make_button(root, "设置", COLOR_BREAK, COLOR_BREAK_2, 468, 18, 78, 136, settings_event,
                NULL);
    make_button(root, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 553, 18, 78, 136, home_event,
                NULL);
}

static void settings_refresh_async(void *user_data)
{
    (void)user_data;
    show_settings();
}

static void button_sound_switch_event(lv_event_t *event)
{
    lv_obj_t *sound_switch = lv_event_get_target_obj(event);
    const bool enabled = lv_obj_has_state(sound_switch, LV_STATE_CHECKED);
    const bool was_enabled = app_settings_button_sound_enabled();
    if (was_enabled) {
        alarm_audio_click();
    }
    const esp_err_t err = app_settings_set_button_sound(enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save button sound setting failed: %s", esp_err_to_name(err));
    }
    if (!was_enabled && enabled) {
        alarm_audio_click();
    }
    lv_async_call(settings_refresh_async, NULL);
}

static void volume_slider_event(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    const uint8_t volume = (uint8_t)lv_slider_get_value(slider);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        alarm_audio_set_volume(volume);
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
        if (app_settings_button_sound_enabled()) {
            alarm_audio_click();
        }
    }
}

static void show_settings(void)
{
    s_screen = SCREEN_SETTINGS;
    lv_obj_t *root = prepare_screen();
    make_label(root, "设置", FONT_CJK, COLOR_TEXT, 12, 18, 126, 28, LV_TEXT_ALIGN_CENTER);
    make_label(root, "个性与声音", FONT_CJK, COLOR_MUTED, 12, 55, 126, 24,
               LV_TEXT_ALIGN_CENTER);
    make_button(root, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 24, 111, 102, 43, menu_event,
                NULL);

    lv_obj_t *panel = make_panel(root, 157, 16, 455, 140, COLOR_CHINESE, COLOR_PANEL_DARK);
    make_label(panel, "按键声音", FONT_CJK, COLOR_TEXT, 24, 12, 170, 27, LV_TEXT_ALIGN_LEFT);
    make_label(panel, app_settings_button_sound_enabled() ? "已开启" : "已关闭", FONT_CJK,
               app_settings_button_sound_enabled() ? COLOR_BREAK : COLOR_MUTED,
               24, 40, 170, 23, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *sound_switch = lv_switch_create(panel);
    lv_obj_set_pos(sound_switch, 342, 13);
    lv_obj_set_size(sound_switch, 80, 43);
    lv_obj_set_style_bg_color(sound_switch, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sound_switch, lv_color_hex(COLOR_BREAK),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sound_switch, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    if (app_settings_button_sound_enabled()) {
        lv_obj_add_state(sound_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sound_switch, button_sound_switch_event, LV_EVENT_VALUE_CHANGED, NULL);

    make_shape(panel, 20, 70, 405, 1, COLOR_CHINESE, LV_OPA_40, 0);
    char volume_text[20];
    snprintf(volume_text, sizeof(volume_text), "音量 %u%%", app_settings_volume());
    s_volume_label = make_label(panel, volume_text, FONT_CJK, COLOR_TEXT, 24, 88, 100, 25,
                                LV_TEXT_ALIGN_LEFT);
    lv_obj_t *volume_slider = lv_slider_create(panel);
    lv_obj_set_pos(volume_slider, 132, 88);
    lv_obj_set_size(volume_slider, 290, 20);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, app_settings_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_BREAK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(volume_slider, 5, LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider, volume_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_slider_event, LV_EVENT_RELEASED, NULL);
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

static void schedule_day_event(lv_event_t *event)
{
    s_schedule_day = (uint8_t)(intptr_t)lv_event_get_user_data(event);
    show_schedule();
}

static void schedule_gesture_event(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if (direction == LV_DIR_LEFT) {
        s_schedule_day = (uint8_t)((s_schedule_day + 1U) % 5U);
    } else if (direction == LV_DIR_RIGHT) {
        s_schedule_day = s_schedule_day == 0 ? 4 : (uint8_t)(s_schedule_day - 1U);
    } else {
        return;
    }
    lv_indev_wait_release(indev);
    if (app_settings_button_sound_enabled()) {
        alarm_audio_click();
    }
    show_schedule();
}

static void show_schedule(void)
{
    s_screen = SCREEN_SCHEDULE;
    lv_obj_t *root = prepare_screen();
    lv_obj_add_event_cb(root, schedule_gesture_event, LV_EVENT_GESTURE, NULL);
    make_label(root, "本周课程表", FONT_CJK, COLOR_TEXT, 9, 9, 115, 25,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, s_weekday_names[s_schedule_day], FONT_CJK, COLOR_MATH, 10, 38, 112, 23,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, "共 7 节课", FONT_CJK, COLOR_MUTED, 10, 66, 112, 23,
               LV_TEXT_ALIGN_CENTER);
    make_button(root, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 18, 111, 96, 42, menu_event,
                NULL);

    for (uint8_t day = 0; day < 5; ++day) {
        char short_day[4];
        snprintf(short_day, sizeof(short_day), "%u", (unsigned)(day + 1));
        const uint32_t color = day == s_schedule_day ? COLOR_CHINESE : COLOR_PANEL;
        make_button(root, short_day, color,
                    day == s_schedule_day ? COLOR_CHINESE_2 : COLOR_PANEL_DARK,
                    132 + day * 48, 8, 40, 35, schedule_day_event,
                    (void *)(intptr_t)day);
    }

    for (uint8_t period = 0; period < 7; ++period) {
        const char *course = s_schedule[s_schedule_day][period];
        const uint32_t color = course_color(course);
        lv_obj_t *card = make_panel(root, 128 + period * 72, 52, 66, 108, color,
                                    course_dark_color(color));
        char period_text[4];
        snprintf(period_text, sizeof(period_text), "%u", (unsigned)(period + 1));
        make_label(card, period_text, LV_FONT_DEFAULT, COLOR_MUTED, 5, 4, 15, 16,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, course, FONT_CJK, COLOR_TEXT, 3, 39, 60, 25, LV_TEXT_ALIGN_CENTER);
        make_label(card, "课程", FONT_CJK, COLOR_MUTED, 3, 76, 60, 20, LV_TEXT_ALIGN_CENTER);
    }
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
            return "选择网络";
    }
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

    make_label(root, "连接网络", FONT_CJK, COLOR_TEXT, 10, 8, 196, 25,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, "设置 · Wi-Fi", FONT_CJK, COLOR_MUTED, 10, 34, 196, 21,
               LV_TEXT_ALIGN_CENTER);
    lv_obj_t *status_panel = make_panel(root, 10, 59, 196, 103,
                                        status.state == WIFI_MANAGER_CONNECTED ? COLOR_BREAK
                                                                                : COLOR_CHINESE,
                                        status.state == WIFI_MANAGER_CONNECTED ? COLOR_BREAK_2
                                                                                : COLOR_CHINESE_2);
    make_label(status_panel, wifi_state_text(status.state), FONT_CJK, COLOR_BREAK, 8, 8, 180, 20,
               LV_TEXT_ALIGN_CENTER);
    make_label(status_panel, status.ssid[0] != '\0' ? status.ssid : "尚未连接", FONT_CJK,
               COLOR_TEXT, 8, 35, 180, 22, LV_TEXT_ALIGN_CENTER);
    char detail[48];
    if (status.state == WIFI_MANAGER_CONNECTED) {
        snprintf(detail, sizeof(detail), "%s", status.ip);
    } else {
        snprintf(detail, sizeof(detail), "仅支持 2.4 GHz");
    }
    make_label(status_panel, detail, FONT_CJK, COLOR_MUTED, 6, 68,
               status.state == WIFI_MANAGER_CONNECTED ? 108 : 184, 20,
               LV_TEXT_ALIGN_CENTER);

    make_label(root, "选择一个网络", FONT_CJK, COLOR_TEXT, 218, 12, 194, 24,
               LV_TEXT_ALIGN_LEFT);
    make_button(root, "扫描", COLOR_CHINESE, COLOR_CHINESE_2, 420, 4, 100, 40,
                wifi_scan_event, NULL);
    make_button(root, "返回", COLOR_PANEL, COLOR_PANEL_DARK, 528, 4, 102, 40, menu_event,
                NULL);

    if (status.state == WIFI_MANAGER_CONNECTED) {
        make_button(status_panel, "断开", COLOR_DANGER, COLOR_DANGER_2, 117, 65, 68, 28,
                    wifi_disconnect_event, NULL);
    }
    if (s_wifi_page_count == 0) {
        make_label(root, status.state == WIFI_MANAGER_SCANNING ? "正在扫描，请稍候"
                                                               : "没有发现可用网络",
                   FONT_CJK, COLOR_MUTED, 218, 82, 412, 28, LV_TEXT_ALIGN_CENTER);
    }
    const size_t visible = s_wifi_page_count > 3 ? 3 : s_wifi_page_count;
    for (size_t index = 0; index < visible; ++index) {
        const uint32_t colors[] = {COLOR_CHINESE, COLOR_BREAK, COLOR_ENGLISH};
        const uint32_t darks[] = {COLOR_CHINESE_2, COLOR_BREAK_2, COLOR_ENGLISH_2};
        make_button(root, s_wifi_page_networks[index].ssid, colors[index], darks[index],
                    218, 49 + (int)index * 40,
                    412, 36, wifi_network_event, (void *)(intptr_t)index);
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
    make_label(root, "输入 Wi-Fi 密码", FONT_CJK, COLOR_TEXT, 5, 7, 184, 24,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, s_selected_ssid, LV_FONT_DEFAULT, COLOR_MUTED, 6, 31, 182, 20,
               LV_TEXT_ALIGN_CENTER);

    s_password_textarea = lv_textarea_create(root);
    lv_obj_set_pos(s_password_textarea, 8, 55);
    lv_obj_set_size(s_password_textarea, 178, 43);
    lv_textarea_set_one_line(s_password_textarea, true);
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_textarea_set_max_length(s_password_textarea, 63);
    lv_textarea_set_placeholder_text(s_password_textarea, "请输入密码");
    lv_obj_set_style_bg_color(s_password_textarea, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(s_password_textarea, lv_color_hex(COLOR_CHINESE), 0);
    lv_obj_set_style_border_width(s_password_textarea, 2, 0);
    lv_obj_set_style_text_color(s_password_textarea, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_password_textarea, FONT_CJK, 0);

    make_button(root, "取消", COLOR_PANEL, COLOR_PANEL_DARK, 8, 109, 84, 51,
                wifi_event, NULL);
    make_button(root, "连接", COLOR_CHINESE, COLOR_CHINESE_2, 99, 109, 87, 51,
                wifi_connect_button_event, NULL);

    lv_obj_t *keyboard = lv_keyboard_create(root);
    lv_obj_set_pos(keyboard, 194, 2);
    lv_obj_set_size(keyboard, 438, 168);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, s_keyboard_lower_map,
                        s_keyboard_ctrl_map);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, s_keyboard_upper_map,
                        s_keyboard_ctrl_map);
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
    (void)event;
    alarm_config_t config = *alarm_store_get();
    config.enabled = lv_obj_has_state(s_alarm_switch, LV_STATE_CHECKED);
    save_alarm_and_refresh(&config);
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

    make_label(root, "学习闹钟", FONT_CJK, COLOR_TEXT, 12, 17, 113, 25,
               LV_TEXT_ALIGN_CENTER);
    make_label(root, config->enabled ? "闹钟已开启" : "闹钟已关闭", FONT_CJK,
               config->enabled ? COLOR_BREAK : COLOR_MUTED, 8, 51, 121, 23,
               LV_TEXT_ALIGN_CENTER);
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
    make_label(root, hour, FONT_TIMER, COLOR_TEXT, 137, 49, 94, 58, LV_TEXT_ALIGN_CENTER);
    make_button(root, "-", COLOR_PANEL, COLOR_PANEL_DARK, 148, 122, 72, 34,
                alarm_adjust_event, (void *)(intptr_t)ALARM_HOUR_DOWN);
    make_label(root, ":", FONT_CLOCK, COLOR_TEXT, 224, 56, 30, 44, LV_TEXT_ALIGN_CENTER);
    make_button(root, "+", COLOR_CHINESE, COLOR_CHINESE_2, 258, 10, 72, 34,
                alarm_adjust_event, (void *)(intptr_t)ALARM_MINUTE_UP);
    make_label(root, minute, FONT_TIMER, COLOR_TEXT, 247, 49, 94, 58, LV_TEXT_ALIGN_CENTER);
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
    lv_obj_add_event_cb(s_alarm_switch, alarm_switch_event, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(settings, "重复", FONT_CJK, COLOR_MUTED, 12, 61, 60, 23, LV_TEXT_ALIGN_LEFT);
    make_button(settings, config->days_mask == 0x7F ? "每天" : "工作日", COLOR_CHINESE,
                COLOR_CHINESE_2, 89, 53, 154, 39, alarm_repeat_event, NULL);
    make_label(settings, "分钟按 5 分钟调整", FONT_CJK, COLOR_MUTED, 12, 108, 231, 22,
               LV_TEXT_ALIGN_CENTER);
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
    make_label(bell, "闹钟响铃", FONT_CJK, COLOR_TEXT, 5, 78, 105, 25,
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
    s_alarm_return_screen = s_screen;
    const esp_err_t err = alarm_audio_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "alarm audio start failed: %s", esp_err_to_name(err));
    }
    show_alarm_ring();
}

static void ui_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_screen == SCREEN_TIMER && s_session.active) {
        const uint32_t elapsed = session_elapsed_seconds();
        if (elapsed != s_last_rendered_second) {
            char time_text[16];
            format_hms(elapsed, time_text, sizeof(time_text));
            lv_label_set_text(s_timer_label, time_text);
            lv_bar_set_value(s_progress_bar, (int32_t)(elapsed % 3600U), LV_ANIM_OFF);
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

    const int64_t now_ms = uptime_ms();
    if (s_snooze_active && now_ms >= s_snooze_due_ms) {
        s_snooze_active = false;
        trigger_alarm();
    }

    if (now_ms - s_last_rtc_poll_ms >= 1000) {
        s_last_rtc_poll_ms = now_ms;
        if (rtc_pcf85063_read(&s_now)) {
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
