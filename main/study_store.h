#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rtc_pcf85063.h"

typedef enum {
    STUDY_SUBJECT_CHINESE = 0,
    STUDY_SUBJECT_MATH,
    STUDY_SUBJECT_ENGLISH,
    STUDY_SUBJECT_BREAK,
    STUDY_SUBJECT_COUNT,
} study_subject_t;

typedef struct {
    int32_t monday_day;
    uint32_t subject_seconds[3];
    uint32_t break_seconds;
    uint16_t sessions[3];
    int32_t completion_day;
    uint8_t completion_mask;
    uint8_t reserved[3];
} study_week_t;

esp_err_t study_store_init(const rtc_datetime_t *now);
esp_err_t study_store_refresh_period(const rtc_datetime_t *now);
esp_err_t study_store_add_session(study_subject_t subject, uint32_t elapsed_seconds,
                                  const rtc_datetime_t *now);
const study_week_t *study_store_current(void);
const study_week_t *study_store_previous(void);
uint32_t study_store_focus_seconds(void);
int32_t study_days_from_civil(int year, unsigned month, unsigned day);
uint8_t study_monday_weekday(int32_t civil_day);
