#include "study_store.h"

#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

#define STORE_MAGIC   0x53545544U
#define STORE_VERSION 1U
#define STORE_NS      "study_timer"
#define STORE_KEY     "weeks"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    study_week_t current;
    study_week_t previous;
} persistent_store_t;

static const char *TAG = "study_store";
static nvs_handle_t s_nvs;
static persistent_store_t s_store;

int32_t study_days_from_civil(int year, unsigned month, unsigned day)
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

uint8_t study_monday_weekday(int32_t civil_day)
{
    int weekday = (civil_day + 3) % 7;
    return (uint8_t)(weekday < 0 ? weekday + 7 : weekday);
}

static int32_t monday_anchor(const rtc_datetime_t *time)
{
    const int32_t day = study_days_from_civil(time->year, time->month, time->day);
    return day - study_monday_weekday(day);
}

static void reset_week(study_week_t *week, int32_t monday, int32_t today)
{
    memset(week, 0, sizeof(*week));
    week->monday_day = monday;
    week->completion_day = today;
}

static esp_err_t save_store(void)
{
    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs, STORE_KEY, &s_store, sizeof(s_store)), TAG,
                        "write NVS");
    return nvs_commit(s_nvs);
}

esp_err_t study_store_init(const rtc_datetime_t *now)
{
    ESP_RETURN_ON_FALSE(now != NULL, ESP_ERR_INVALID_ARG, TAG, "time required");
    ESP_RETURN_ON_ERROR(nvs_open(STORE_NS, NVS_READWRITE, &s_nvs), TAG, "open NVS");

    size_t stored_size = sizeof(s_store);
    const esp_err_t read_result = nvs_get_blob(s_nvs, STORE_KEY, &s_store, &stored_size);
    const bool valid = read_result == ESP_OK && stored_size == sizeof(s_store) &&
                       s_store.magic == STORE_MAGIC && s_store.version == STORE_VERSION &&
                       s_store.size == sizeof(s_store);
    if (!valid) {
        memset(&s_store, 0, sizeof(s_store));
        s_store.magic = STORE_MAGIC;
        s_store.version = STORE_VERSION;
        s_store.size = sizeof(s_store);
        const int32_t today = study_days_from_civil(now->year, now->month, now->day);
        reset_week(&s_store.current, monday_anchor(now), today);
        ESP_LOGI(TAG, "created a new study data store");
        return save_store();
    }
    return study_store_refresh_period(now);
}

esp_err_t study_store_refresh_period(const rtc_datetime_t *now)
{
    ESP_RETURN_ON_FALSE(now != NULL, ESP_ERR_INVALID_ARG, TAG, "time required");
    const int32_t today = study_days_from_civil(now->year, now->month, now->day);
    const int32_t monday = monday_anchor(now);
    bool changed = false;

    if (s_store.current.monday_day != monday) {
        s_store.previous = s_store.current;
        reset_week(&s_store.current, monday, today);
        changed = true;
    } else if (s_store.current.completion_day != today) {
        s_store.current.completion_day = today;
        s_store.current.completion_mask = 0;
        changed = true;
    }
    return changed ? save_store() : ESP_OK;
}

esp_err_t study_store_add_session(study_subject_t subject, uint32_t elapsed_seconds,
                                  const rtc_datetime_t *now)
{
    ESP_RETURN_ON_FALSE(subject < STUDY_SUBJECT_COUNT && now != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid session");
    ESP_RETURN_ON_ERROR(study_store_refresh_period(now), TAG, "refresh period");
    if (elapsed_seconds == 0) {
        return ESP_OK;
    }

    if (subject == STUDY_SUBJECT_BREAK) {
        s_store.current.break_seconds += elapsed_seconds;
    } else {
        const uint8_t index = (uint8_t)subject;
        s_store.current.subject_seconds[index] += elapsed_seconds;
        s_store.current.sessions[index]++;
        s_store.current.completion_mask |= (uint8_t)(1U << index);
    }
    return save_store();
}

const study_week_t *study_store_current(void)
{
    return &s_store.current;
}

const study_week_t *study_store_previous(void)
{
    return &s_store.previous;
}

uint32_t study_store_focus_seconds(void)
{
    return s_store.current.subject_seconds[0] + s_store.current.subject_seconds[1] +
           s_store.current.subject_seconds[2];
}
