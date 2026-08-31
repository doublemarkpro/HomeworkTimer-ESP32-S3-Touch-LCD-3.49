#include "study_ui.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "rtc_pcf85063.h"
#include "study_store.h"

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

#define COLOR_BG      0x08111F
#define COLOR_PANEL   0x13233E
#define COLOR_TEXT    0xF5F7FB
#define COLOR_MUTED   0x91A0B8
#define COLOR_CHINESE 0x4E8FF7
#define COLOR_MATH    0xF3B51B
#define COLOR_ENGLISH 0x8669E8
#define COLOR_BREAK   0x42CFA3
#define COLOR_DANGER  0xF16E75

typedef enum {
    SCREEN_HOME,
    SCREEN_TIMER,
    SCREEN_REPORT,
} screen_id_t;

typedef struct {
    study_subject_t subject;
    bool active;
    bool running;
    uint32_t accumulated_seconds;
    int64_t started_ms;
} active_session_t;

static const char *TAG = "study_ui";
static const char *const s_subject_names[] = {"语文", "数学", "英语", "休息"};
static const uint32_t s_subject_colors[] = {
    COLOR_CHINESE, COLOR_MATH, COLOR_ENGLISH, COLOR_BREAK,
};
static const char *const s_weekday_names[] = {
    "周一", "周二", "周三", "周四", "周五", "周六", "周日",
};

static active_session_t s_session;
static screen_id_t s_screen;
static rtc_datetime_t s_now;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_timer_status_label;
static lv_obj_t *s_pause_label;
static lv_obj_t *s_progress_bar;
static uint32_t s_last_rendered_second = UINT32_MAX;
static uint8_t s_last_clock_minute = UINT8_MAX;
static int64_t s_last_rtc_poll_ms;

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

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t color, int x, int y,
                             int width, int height, lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_lighten(lv_color_hex(color), 40), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FONT_CJK, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *prepare_screen(void)
{
    lv_obj_t *root = lv_screen_active();
    lv_obj_clean(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    s_clock_label = NULL;
    s_timer_label = NULL;
    s_timer_status_label = NULL;
    s_pause_label = NULL;
    s_progress_bar = NULL;
    return root;
}

static void show_home(void);
static void show_timer(void);
static void show_report(void);

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

static void home_event(lv_event_t *event)
{
    (void)event;
    show_home();
}

static void show_home(void)
{
    if (rtc_pcf85063_read(&s_now)) {
        study_store_refresh_period(&s_now);
    }
    const study_week_t *week = study_store_current();
    s_screen = SCREEN_HOME;
    lv_obj_t *root = prepare_screen();

    s_clock_label = make_label(root, "--:--", FONT_CLOCK, COLOR_TEXT, 10, 20, 108, 42,
                               LV_TEXT_ALIGN_CENTER);
    char date_text[32];
    const int32_t civil_day = study_days_from_civil(s_now.year, s_now.month, s_now.day);
    const uint8_t weekday = study_monday_weekday(civil_day);
    snprintf(date_text, sizeof(date_text), "%u月%u日 %s", s_now.month, s_now.day,
             s_weekday_names[weekday]);
    make_label(root, date_text, FONT_CJK, COLOR_MUTED, 5, 64, 118, 24, LV_TEXT_ALIGN_CENTER);

    const char *status = (week->completion_mask & 0x07) == 0x07
                             ? "全部完成 真棒!"
                             : (weekday == 4 ? "周五 查看周报" : "选择科目开始");
    make_label(root, status, FONT_CJK, COLOR_BREAK, 4, 110, 120, 25, LV_TEXT_ALIGN_CENTER);

    static const int subject_x[] = {128, 247, 366};
    for (uint8_t index = 0; index < 3; ++index) {
        char text[48];
        snprintf(text, sizeof(text), "%s\n%" PRIu32 "分", s_subject_names[index],
                 (week->subject_seconds[index] + 30U) / 60U);
        make_button(root, text, s_subject_colors[index], subject_x[index], 13, 110, 146,
                    start_subject_event, (void *)(intptr_t)index);
    }
    make_button(root, "休息", COLOR_BREAK, 485, 13, 70, 146, start_subject_event,
                (void *)(intptr_t)STUDY_SUBJECT_BREAK);
    make_button(root, "周报", COLOR_PANEL, 563, 13, 68, 146, report_event, NULL);
    s_last_clock_minute = UINT8_MAX;
}

static void show_timer(void)
{
    s_screen = SCREEN_TIMER;
    lv_obj_t *root = prepare_screen();
    const uint32_t accent = s_subject_colors[s_session.subject];
    make_label(root, s_session.subject == STUDY_SUBJECT_BREAK ? "休息一下"
                                                              : s_subject_names[s_session.subject],
               FONT_CJK, accent, 12, 24, 112, 30, LV_TEXT_ALIGN_CENTER);
    s_timer_status_label = make_label(root, "正在计时", FONT_CJK, COLOR_MUTED, 12, 61, 112,
                                      26, LV_TEXT_ALIGN_CENTER);

    const study_week_t *week = study_store_current();
    char weekly[40];
    if (s_session.subject == STUDY_SUBJECT_BREAK) {
        snprintf(weekly, sizeof(weekly), "本周休息 %" PRIu32 "分",
                 (week->break_seconds + 30U) / 60U);
    } else {
        const uint8_t index = (uint8_t)s_session.subject;
        snprintf(weekly, sizeof(weekly), "本周第%u段", (unsigned)(week->sessions[index] + 1));
    }
    make_label(root, weekly, FONT_CJK, COLOR_MUTED, 8, 105, 120, 25, LV_TEXT_ALIGN_CENTER);

    s_timer_label = make_label(root, "00:00:00", FONT_TIMER, COLOR_TEXT, 137, 33, 250, 62,
                               LV_TEXT_ALIGN_CENTER);
    s_progress_bar = lv_bar_create(root);
    lv_obj_set_pos(s_progress_bar, 151, 119);
    lv_obj_set_size(s_progress_bar, 222, 10);
    lv_bar_set_range(s_progress_bar, 0, 3600);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(accent), LV_PART_INDICATOR);

    lv_obj_t *pause_button = lv_button_create(root);
    lv_obj_set_pos(pause_button, 405, 27);
    lv_obj_set_size(pause_button, 98, 118);
    lv_obj_set_style_radius(pause_button, 14, 0);
    lv_obj_set_style_bg_color(pause_button, lv_color_hex(COLOR_DANGER), 0);
    lv_obj_add_event_cb(pause_button, pause_event, LV_EVENT_CLICKED, NULL);
    s_pause_label = lv_label_create(pause_button);
    lv_label_set_text(s_pause_label, "暂停");
    lv_obj_set_style_text_font(s_pause_label, FONT_CJK, 0);
    lv_obj_set_style_text_color(s_pause_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(s_pause_label);

    make_button(root, s_session.subject == STUDY_SUBJECT_BREAK ? "结束休息" : "完成本科",
                COLOR_CHINESE, 514, 27, 116, 118, finish_event, NULL);
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
    make_label(root, total, FONT_VALUE, COLOR_BREAK, 55, 47, 88, 30, LV_TEXT_ALIGN_CENTER);
    char rest[40];
    format_minutes(week->break_seconds, rest, sizeof(rest));
    make_label(root, "休息", FONT_CJK, COLOR_MUTED, 15, 94, 45, 22, LV_TEXT_ALIGN_LEFT);
    make_label(root, rest, FONT_CJK, COLOR_BREAK, 55, 93, 88, 24, LV_TEXT_ALIGN_CENTER);

    static const int card_x[] = {151, 285, 419};
    for (uint8_t index = 0; index < 3; ++index) {
        lv_obj_t *card = lv_obj_create(root);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(card, card_x[index], 16);
        lv_obj_set_size(card, 124, 140);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(s_subject_colors[index]), 0);
        lv_obj_set_style_border_width(card, 2, 0);
        make_label(card, s_subject_names[index], FONT_CJK, s_subject_colors[index], 4, 10, 112,
                   25, LV_TEXT_ALIGN_CENTER);
        char value[40];
        format_minutes(week->subject_seconds[index], value, sizeof(value));
        make_label(card, value, FONT_VALUE, COLOR_TEXT, 3, 48, 114, 31, LV_TEXT_ALIGN_CENTER);
        char sessions[32];
        snprintf(sessions, sizeof(sessions), "%u段", week->sessions[index]);
        make_label(card, sessions, FONT_CJK, COLOR_MUTED, 4, 94, 112, 24,
                   LV_TEXT_ALIGN_CENTER);
    }
    make_button(root, "返回", COLOR_CHINESE, 557, 45, 73, 83, home_event, NULL);
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

    const int64_t now_ms = uptime_ms();
    if (s_screen == SCREEN_HOME && now_ms - s_last_rtc_poll_ms >= 1000) {
        s_last_rtc_poll_ms = now_ms;
        if (rtc_pcf85063_read(&s_now) && s_now.minute != s_last_clock_minute) {
            char clock_text[8];
            snprintf(clock_text, sizeof(clock_text), "%02u:%02u", s_now.hour, s_now.minute);
            lv_label_set_text(s_clock_label, clock_text);
            s_last_clock_minute = s_now.minute;
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
