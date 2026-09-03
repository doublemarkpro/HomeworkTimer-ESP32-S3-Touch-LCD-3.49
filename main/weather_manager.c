#include "weather_manager.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "miniz.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "weather_config.h"
#include "wifi_manager.h"

#define WEATHER_RESPONSE_CAPACITY 4096
#define WEATHER_DECODED_CAPACITY 12288
#define WEATHER_REFRESH_MS (30LL * 60LL * 1000LL)
#define WEATHER_RETRY_MS (5LL * 60LL * 1000LL)

typedef struct {
    char data[WEATHER_RESPONSE_CAPACITY];
    size_t length;
    bool overflowed;
} weather_response_t;

static const char *TAG = "weather_manager";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static weather_status_t s_status;
/* Keep the HTTP body out of the weather task stack: TLS already needs a
 * comparatively large stack on ESP32-S3.  Only the weather task accesses it. */
static weather_response_t s_response;
static char s_decoded_response[WEATHER_DECODED_CAPACITY];
static TaskHandle_t s_task;
static bool s_refresh_requested;
static int64_t s_next_fetch_ms;

static bool weather_is_configured(void)
{
    return QWEATHER_API_HOST[0] != '\0' && QWEATHER_API_KEY[0] != '\0' &&
           strstr(QWEATHER_API_HOST, "your-") == NULL &&
           QWEATHER_LATITUDE[0] != '\0' && QWEATHER_LONGITUDE[0] != '\0';
}

static void set_state(weather_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    if (s_status.state != state) {
        s_status.state = state;
        s_status.generation++;
    }
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t weather_http_event(esp_http_client_event_t *event)
{
    weather_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL || event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t available = sizeof(response->data) - response->length - 1;
    const size_t copy_length = (size_t)event->data_len < available
                                   ? (size_t)event->data_len
                                   : available;
    if (copy_length > 0) {
        memcpy(response->data + response->length, event->data, copy_length);
        response->length += copy_length;
        response->data[response->length] = '\0';
    }
    if (copy_length != (size_t)event->data_len) {
        response->overflowed = true;
    }
    return ESP_OK;
}

static const char *weather_payload(void)
{
    const uint8_t *input = (const uint8_t *)s_response.data;
    if (s_response.length < 2 || input[0] != 0x1f || input[1] != 0x8b) {
        return s_response.data;
    }
    if (s_response.length < 18 || input[2] != 8 || (input[3] & 0xe0) != 0) {
        return NULL;
    }

    const uint8_t flags = input[3];
    size_t position = 10;
    if ((flags & 0x04) != 0) {
        if (position + 2 > s_response.length) {
            return NULL;
        }
        const size_t extra_length = input[position] | ((size_t)input[position + 1] << 8);
        position += 2;
        if (position + extra_length > s_response.length) {
            return NULL;
        }
        position += extra_length;
    }
    for (uint8_t flag = 0x08; flag <= 0x10; flag <<= 1) {
        if ((flags & flag) != 0) {
            while (position < s_response.length && input[position++] != '\0') {
            }
            if (position >= s_response.length) {
                return NULL;
            }
        }
    }
    if ((flags & 0x02) != 0) {
        position += 2;
    }
    if (position + 8 >= s_response.length) {
        return NULL;
    }

    const size_t compressed_length = s_response.length - position - 8;
    const size_t decoded_length = tinfl_decompress_mem_to_mem(
        s_decoded_response, sizeof(s_decoded_response) - 1,
        input + position, compressed_length, 0);
    if (decoded_length == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ||
        decoded_length >= sizeof(s_decoded_response)) {
        return NULL;
    }
    s_decoded_response[decoded_length] = '\0';
    return s_decoded_response;
}

static const char *find_within(const char *begin, const char *end, const char *needle)
{
    const char *match = strstr(begin, needle);
    return match != NULL && match < end ? match : NULL;
}

static const char *json_object_end(const char *begin, const char *limit)
{
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char *cursor = begin; cursor < limit; ++cursor) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (*cursor == '\\') {
                escaped = true;
            } else if (*cursor == '"') {
                in_string = false;
            }
            continue;
        }
        if (*cursor == '"') {
            in_string = true;
        } else if (*cursor == '{') {
            depth++;
        } else if (*cursor == '}' && --depth == 0) {
            return cursor + 1;
        }
    }
    return NULL;
}

static bool parse_value_after_key(const char *begin, const char *end,
                                  const char *key, double *value)
{
    const char *key_position = find_within(begin, end, key);
    const char *value_position = key_position == NULL
                                     ? NULL
                                     : find_within(key_position, end, "\"value\"");
    const char *colon = value_position == NULL ? NULL : strchr(value_position, ':');
    if (colon == NULL || colon >= end) {
        return false;
    }
    char *value_end = NULL;
    const double parsed = strtod(colon + 1, &value_end);
    if (value_end == colon + 1 || value_end > end || !isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_forecast_day(const char *begin, const char *end,
                               weather_forecast_day_t *forecast)
{
    const char *date_key = find_within(begin, end, "\"forecastStartTime\"");
    const char *date_colon = date_key == NULL ? NULL : strchr(date_key, ':');
    int month = 0;
    int day = 0;
    if (date_colon == NULL || date_colon >= end ||
        sscanf(date_colon + 1, "\"%*d-%d-%d", &month, &day) != 2 ||
        month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    double maximum = 0;
    double minimum = 0;
    if (!parse_value_after_key(begin, end, "\"temperatureMax\"", &maximum) ||
        !parse_value_after_key(begin, end, "\"temperatureMin\"", &minimum)) {
        return false;
    }

    const char *daytime = find_within(begin, end, "\"daytime\"");
    const char *condition = daytime == NULL
                                ? NULL
                                : find_within(daytime, end, "\"condition\"");
    const char *code_key = condition == NULL
                               ? NULL
                               : find_within(condition, end, "\"code\"");
    const char *code_colon = code_key == NULL ? NULL : strchr(code_key, ':');
    if (code_colon == NULL || code_colon >= end) {
        return false;
    }
    do {
        code_colon++;
    } while (code_colon < end &&
             (*code_colon == ' ' || *code_colon == '\t' || *code_colon == '"'));
    char *code_end = NULL;
    const long parsed_code = strtol(code_colon, &code_end, 10);
    if (code_end == code_colon || code_end > end || parsed_code < 0 ||
        parsed_code > UINT16_MAX) {
        return false;
    }

    forecast->month = (uint8_t)month;
    forecast->day = (uint8_t)day;
    forecast->temperature_min_c = (int16_t)lround(minimum);
    forecast->temperature_max_c = (int16_t)lround(maximum);
    forecast->icon_code = (uint16_t)parsed_code;
    return true;
}

static esp_err_t fetch_weather(weather_forecast_day_t forecast[WEATHER_FORECAST_DAYS],
                               uint8_t *day_count)
{
    char url[256];
    const int url_length = snprintf(url, sizeof(url),
                                    "https://%s/weather/v1/daily/%s/%s?days=3&localTime=true&lang=zh",
                                    QWEATHER_API_HOST, QWEATHER_LATITUDE,
                                    QWEATHER_LONGITUDE);
    if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(&s_response, 0, sizeof(s_response));
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = weather_http_event,
        .user_data = &s_response,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "X-QW-Api-Key", QWEATHER_API_KEY);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "gzip");

    ESP_LOGI(TAG, "requesting 3-day forecast");
    esp_err_t err = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "weather request failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status_code != 200 || s_response.overflowed || s_response.length == 0) {
        ESP_LOGW(TAG, "weather response rejected: HTTP %d, %u bytes%s", status_code,
                 (unsigned)s_response.length, s_response.overflowed ? ", overflow" : "");
        return ESP_FAIL;
    }

    const char *payload = weather_payload();
    if (payload == NULL) {
        ESP_LOGW(TAG, "weather gzip decode failed");
        return ESP_ERR_INVALID_RESPONSE;
    }
    const char *days_key = strstr(payload, "\"days\"");
    const char *cursor = days_key == NULL ? NULL : strchr(days_key, '[');
    if (cursor == NULL) {
        ESP_LOGW(TAG, "forecast days missing");
        return ESP_ERR_INVALID_RESPONSE;
    }
    cursor++;
    const char *payload_end = payload + strlen(payload);
    uint8_t parsed_days = 0;
    while (parsed_days < WEATHER_FORECAST_DAYS) {
        while (cursor < payload_end && (*cursor == ' ' || *cursor == '\r' ||
                                        *cursor == '\n' || *cursor == '\t' ||
                                        *cursor == ',')) {
            cursor++;
        }
        if (cursor >= payload_end || *cursor != '{') {
            break;
        }
        const char *day_end = json_object_end(cursor, payload_end);
        if (day_end == NULL ||
            !parse_forecast_day(cursor, day_end, &forecast[parsed_days])) {
            ESP_LOGW(TAG, "forecast day %u invalid", (unsigned)parsed_days);
            return ESP_ERR_INVALID_RESPONSE;
        }
        parsed_days++;
        cursor = day_end;
    }
    if (parsed_days != WEATHER_FORECAST_DAYS) {
        ESP_LOGW(TAG, "forecast returned only %u days", (unsigned)parsed_days);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *day_count = parsed_days;
    return ESP_OK;
}

static void weather_task(void *argument)
{
    (void)argument;
    for (;;) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        bool refresh_requested;
        portENTER_CRITICAL(&s_lock);
        refresh_requested = s_refresh_requested;
        s_refresh_requested = false;
        portEXIT_CRITICAL(&s_lock);

        if (!weather_is_configured()) {
            set_state(WEATHER_CONFIG_REQUIRED);
        } else {
            wifi_manager_status_t wifi_status;
            wifi_manager_get_status(&wifi_status);
            if (wifi_status.state != WIFI_MANAGER_CONNECTED) {
                set_state(WEATHER_WAITING_FOR_WIFI);
            } else if (refresh_requested || s_next_fetch_ms == 0 || now_ms >= s_next_fetch_ms) {
                set_state(WEATHER_LOADING);
                weather_forecast_day_t forecast[WEATHER_FORECAST_DAYS] = {0};
                uint8_t day_count = 0;
                const esp_err_t err = fetch_weather(forecast, &day_count);
                portENTER_CRITICAL(&s_lock);
                if (err == ESP_OK) {
                    memcpy(s_status.days, forecast, sizeof(forecast));
                    s_status.day_count = day_count;
                    s_status.state = WEATHER_READY;
                    s_next_fetch_ms = now_ms + WEATHER_REFRESH_MS;
                } else {
                    s_status.state = WEATHER_FAILED;
                    s_next_fetch_ms = now_ms + WEATHER_RETRY_MS;
                }
                s_status.generation++;
                portEXIT_CRITICAL(&s_lock);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "forecast updated: %u days, stack free %u",
                             (unsigned)day_count,
                             (unsigned)uxTaskGetStackHighWaterMark(NULL));
                }
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
    }
}

esp_err_t weather_manager_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = weather_is_configured() ? WEATHER_WAITING_FOR_WIFI
                                             : WEATHER_CONFIG_REQUIRED;
    s_status.generation = 1;
    if (xTaskCreate(weather_task, "weather", 20480, NULL, 4, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void weather_manager_refresh(void)
{
    portENTER_CRITICAL(&s_lock);
    s_refresh_requested = true;
    portEXIT_CRITICAL(&s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

void weather_manager_get_status(weather_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}
