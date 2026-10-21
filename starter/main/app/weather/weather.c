#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager.h"
#include "https_guard.h"

static const char *TAG = "weather";

#define WEATHER_HTTP_BUF_SIZE  (10 * 1024)  /* response body in PSRAM; keep off TLS path */
#define SRS_HTTP_BUF_SIZE      512
#define WEATHER_URL \
    "https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=rhrread&lang=en"

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} http_accum_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_accum_t *acc = (http_accum_t *)evt->user_data;
    if (acc == NULL || acc->buf == NULL) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        if (acc->len + (size_t)evt->data_len + 1 > acc->cap) {
            ESP_LOGE(TAG, "response too large (%u+%d > %u)",
                     (unsigned)acc->len, evt->data_len, (unsigned)acc->cap);
            return ESP_ERR_NO_MEM;
        }
        memcpy(acc->buf + acc->len, evt->data, (size_t)evt->data_len);
        acc->len += (size_t)evt->data_len;
        acc->buf[acc->len] = '\0';
    }
    return ESP_OK;
}

const char *weather_icon_label(int icon)
{
    /* HKO rhrread icon → Traditional Chinese captions (GenWan subset). */
    switch (icon) {
    case 50: return "晴朗";
    case 51: return "間中有陽光";
    case 52: return "短暫陽光";
    case 53: return "陽光及驟雨";
    case 54: return "短暫陽光及驟雨";
    case 60: return "多雲";
    case 61: return "密雲";
    case 62: return "微雨";
    case 63: return "雨";
    case 64: return "大雨";
    case 65: return "雷暴";
    case 70:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75: return "天色良好";
    case 76: return "大致多雲";
    case 77: return "大致天晴";
    case 80: return "大風";
    case 81: return "乾燥";
    case 82: return "潮濕";
    case 83: return "霧";
    case 84: return "薄霧";
    case 85: return "煙霞";
    case 90: return "炎熱";
    case 91: return "暖和";
    case 92: return "清涼";
    case 93: return "寒冷";
    default: return "天氣";
    }
}

static void extract_hhmm(char *dst, size_t dst_len, const char *iso)
{
    if (dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (iso == NULL) {
        return;
    }
    const char *t = strchr(iso, 'T');
    if (t && strlen(t) >= 6) {
        snprintf(dst, dst_len, "%.5s", t + 1);
    }
}

static bool pick_temp(cJSON *temp_arr, const char *place, int *out_c)
{
    if (!cJSON_IsArray(temp_arr) || place == NULL || out_c == NULL) {
        return false;
    }
    cJSON *item;
    cJSON_ArrayForEach(item, temp_arr) {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(item, "place");
        cJSON *v = cJSON_GetObjectItemCaseSensitive(item, "value");
        if (cJSON_IsString(p) && p->valuestring &&
            strcmp(p->valuestring, place) == 0 && cJSON_IsNumber(v)) {
            *out_c = v->valueint;
            return true;
        }
    }
    return false;
}

#define HTTP_MAX_ATTEMPTS   6
#define HTTP_RETRY_BASE_MS  1000
#define HTTP_TIMEOUT_MS     25000
/* Smaller transport buffers: less internal DRAM beside TLS (SPIRAM holds response body). */
#define HTTP_RX_BUF         1024
#define HTTP_TX_BUF         256

static bool http_err_retryable(esp_err_t err)
{
    return err == ESP_ERR_HTTP_CONNECT ||
           err == ESP_ERR_HTTP_CONNECTING ||
           err == ESP_ERR_TIMEOUT ||
           err == ESP_ERR_HTTP_EAGAIN ||
           err == ESP_ERR_NO_MEM ||
           err == ESP_FAIL;
}

/**
 * HTTPS GET with esp_crt_bundle + retries.
 * -0x008D (MBEDTLS_ERR_SSL_ALLOC_FAILED) often surfaces as CONNECT/FAIL when
 * internal DRAM cannot satisfy ssl_setup / handshake; we serialize via https_guard,
 * settle after cleanup, and back off harder when the heap looks tight.
 */
static esp_err_t http_get(const char *url, char *buf, size_t cap, size_t *out_len)
{
    esp_err_t err = ESP_FAIL;
    int status_code = 0;

    for (int attempt = 1; attempt <= HTTP_MAX_ATTEMPTS; attempt++) {
        http_accum_t acc = { .buf = buf, .len = 0, .cap = cap };
        buf[0] = '\0';

        if (!https_guard_lock(HTTP_TIMEOUT_MS + 8000)) {
            err = ESP_ERR_TIMEOUT;
            ESP_LOGW(TAG, "HTTP attempt %d/%d: could not take HTTPS lock",
                     attempt, HTTP_MAX_ATTEMPTS);
            if (attempt < HTTP_MAX_ATTEMPTS) {
                vTaskDelay(pdMS_TO_TICKS(HTTP_RETRY_BASE_MS * attempt));
                continue;
            }
            break;
        }

        https_guard_log_heap("weather-pre");
        if (!https_guard_wait_internal_room(3000)) {
            https_guard_unlock();
            err = ESP_ERR_NO_MEM;
            ESP_LOGW(TAG, "HTTP attempt %d/%d: defer TLS (heap tight after wait)",
                     attempt, HTTP_MAX_ATTEMPTS);
            if (attempt < HTTP_MAX_ATTEMPTS) {
                vTaskDelay(pdMS_TO_TICKS(HTTP_RETRY_BASE_MS * attempt * 2));
                continue;
            }
            break;
        }

        esp_http_client_config_t cfg = {
            .url = url,
            .event_handler = http_event_handler,
            .user_data = &acc,
            .timeout_ms = HTTP_TIMEOUT_MS,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size = HTTP_RX_BUF,
            .buffer_size_tx = HTTP_TX_BUF,
            .keep_alive_enable = false,
        };

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client == NULL) {
            https_guard_post_tls_settle();
            https_guard_unlock();
            err = ESP_ERR_NO_MEM;
            ESP_LOGW(TAG, "HTTP attempt %d/%d: client init failed",
                     attempt, HTTP_MAX_ATTEMPTS);
            if (attempt < HTTP_MAX_ATTEMPTS) {
                vTaskDelay(pdMS_TO_TICKS(HTTP_RETRY_BASE_MS * attempt * 2));
                continue;
            }
            break;
        }

        err = esp_http_client_perform(client);
        status_code = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        https_guard_post_tls_settle();
        https_guard_unlock();

        if (err == ESP_OK && status_code == 200 && acc.len > 0) {
            if (out_len) {
                *out_len = acc.len;
            }
            if (attempt > 1) {
                ESP_LOGI(TAG, "HTTP OK on attempt %d", attempt);
            }
            return ESP_OK;
        }

        ESP_LOGW(TAG, "HTTP attempt %d/%d failed err=%s status=%d len=%u",
                 attempt, HTTP_MAX_ATTEMPTS, esp_err_to_name(err),
                 status_code, (unsigned)acc.len);

        if (attempt < HTTP_MAX_ATTEMPTS && http_err_retryable(err)) {
            /* Extra pause after connect/OOM — typical -0x008D path */
            unsigned backoff = HTTP_RETRY_BASE_MS * attempt;
            if (err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_NO_MEM || err == ESP_FAIL) {
                backoff *= 2;
            }
            vTaskDelay(pdMS_TO_TICKS(backoff));
            continue;
        }
        break;
    }

    if (err == ESP_OK && (status_code != 200)) {
        return ESP_FAIL;
    }
    return err;
}


static esp_err_t parse_weather_json(const char *json, weather_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    /* Preserve sunrise/sunset if already filled */
    char rise[WEATHER_HHMM_MAX];
    char set[WEATHER_HHMM_MAX];
    strncpy(rise, out->sunrise, sizeof(rise));
    rise[sizeof(rise) - 1] = '\0';
    strncpy(set, out->sunset, sizeof(set));
    set[sizeof(set) - 1] = '\0';

    memset(out, 0, sizeof(*out));
    out->humidity = -1;
    out->icon = -1;
    out->temp_valid = false;
    strncpy(out->sunrise, rise, sizeof(out->sunrise) - 1);
    strncpy(out->sunset, set, sizeof(out->sunset) - 1);

    cJSON *upd = cJSON_GetObjectItemCaseSensitive(root, "updateTime");
    if (cJSON_IsString(upd) && upd->valuestring) {
        extract_hhmm(out->update_hhmm, sizeof(out->update_hhmm), upd->valuestring);
    }

    cJSON *icons = cJSON_GetObjectItemCaseSensitive(root, "icon");
    if (cJSON_IsArray(icons) && cJSON_GetArraySize(icons) > 0) {
        cJSON *first = cJSON_GetArrayItem(icons, 0);
        if (cJSON_IsNumber(first)) {
            out->icon = first->valueint;
            strncpy(out->condition, weather_icon_label(out->icon),
                    sizeof(out->condition) - 1);
        }
    }
    if (out->condition[0] == '\0') {
        strncpy(out->condition, "天氣", sizeof(out->condition) - 1);
    }

    cJSON *temperature = cJSON_GetObjectItemCaseSensitive(root, "temperature");
    cJSON *temp_data = temperature ?
                       cJSON_GetObjectItemCaseSensitive(temperature, "data") : NULL;
    int temp = 0;
    if (pick_temp(temp_data, "Tuen Mun", &temp)) {
        strncpy(out->place, "Tuen Mun", sizeof(out->place) - 1);
        out->temp_c = temp;
        out->temp_valid = true;
    } else if (pick_temp(temp_data, "Yuen Long Park", &temp)) {
        strncpy(out->place, "Yuen Long Park", sizeof(out->place) - 1);
        out->temp_c = temp;
        out->temp_valid = true;
    } else {
        strncpy(out->place, "Tuen Mun", sizeof(out->place) - 1);
    }

    cJSON *humidity = cJSON_GetObjectItemCaseSensitive(root, "humidity");
    cJSON *hum_data = humidity ? cJSON_GetObjectItemCaseSensitive(humidity, "data") : NULL;
    if (cJSON_IsArray(hum_data) && cJSON_GetArraySize(hum_data) > 0) {
        cJSON *h0 = cJSON_GetArrayItem(hum_data, 0);
        cJSON *hv = cJSON_GetObjectItemCaseSensitive(h0, "value");
        if (cJSON_IsNumber(hv)) {
            out->humidity = hv->valueint;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "parsed place=%s temp=%d icon=%d rh=%d upd=%s",
             out->place, out->temp_valid ? out->temp_c : -999,
             out->icon, out->humidity, out->update_hhmm);
    return out->temp_valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/** Parse SRS JSON: fields + data[0] = [date, rise, transit, set]. */
static void parse_srs_json(const char *json, weather_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return;
    }
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data) || cJSON_GetArraySize(data) < 1) {
        cJSON_Delete(root);
        return;
    }
    cJSON *row = cJSON_GetArrayItem(data, 0);
    if (!cJSON_IsArray(row) || cJSON_GetArraySize(row) < 4) {
        cJSON_Delete(root);
        return;
    }
    cJSON *rise = cJSON_GetArrayItem(row, 1);
    cJSON *set = cJSON_GetArrayItem(row, 3);
    if (cJSON_IsString(rise) && rise->valuestring) {
        strncpy(out->sunrise, rise->valuestring, sizeof(out->sunrise) - 1);
        out->sunrise[sizeof(out->sunrise) - 1] = '\0';
    }
    if (cJSON_IsString(set) && set->valuestring) {
        strncpy(out->sunset, set->valuestring, sizeof(out->sunset) - 1);
        out->sunset[sizeof(out->sunset) - 1] = '\0';
    }
    ESP_LOGI(TAG, "SRS sunrise=%s sunset=%s", out->sunrise, out->sunset);
    cJSON_Delete(root);
}

static void fetch_srs_today(weather_data_t *out)
{
    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year < (2020 - 1900)) {
        ESP_LOGW(TAG, "SRS skipped (time not ready)");
        return;
    }

    char url[192];
    snprintf(url, sizeof(url),
             "https://data.weather.gov.hk/weatherAPI/opendata/opendata.php"
             "?dataType=SRS&year=%d&month=%d&day=%d&rformat=json",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);

    char *buf = malloc(SRS_HTTP_BUF_SIZE);
    if (buf == NULL) {
        return;
    }
    if (http_get(url, buf, SRS_HTTP_BUF_SIZE, NULL) == ESP_OK) {
        parse_srs_json(buf, out);
    } else {
        ESP_LOGW(TAG, "SRS fetch failed");
    }
    free(buf);
}

esp_err_t weather_fetch(weather_data_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->humidity = -1;
    out->icon = -1;

    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected");
        return ESP_ERR_INVALID_STATE;
    }

    char *buf = heap_caps_malloc(WEATHER_HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc(WEATHER_HTTP_BUF_SIZE);
    }
    if (buf == NULL) {
        ESP_LOGE(TAG, "alloc %d failed", WEATHER_HTTP_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = http_get(WEATHER_URL, buf, WEATHER_HTTP_BUF_SIZE, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rhrread HTTP failed: %s", esp_err_to_name(err));
        free(buf);
        return err;
    }

    err = parse_weather_json(buf, out);
    free(buf);
    /* Drop large body before second HTTPS (SRS); settle so TLS can reclaim DRAM. */
    https_guard_post_tls_settle();

    /* Best-effort sunrise/sunset (does not fail the whole fetch). */
    fetch_srs_today(out);
    return err;
}