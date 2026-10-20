#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "wifi_manager.h"

static const char *TAG = "weather";

#define WEATHER_HTTP_BUF_SIZE  (10 * 1024)
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
    switch (icon) {
    case 50: return "Sunny";
    case 51: return "Sunny Periods";
    case 52: return "Sunny Intervals";
    case 53: return "Sunny + Showers";
    case 54: return "Intervals + Showers";
    case 60: return "Cloudy";
    case 61: return "Overcast";
    case 62: return "Light Rain";
    case 63: return "Rain";
    case 64: return "Heavy Rain";
    case 65: return "Thunderstorms";
    case 70:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75: return "Fine";
    case 76: return "Mainly Cloudy";
    case 77: return "Mainly Fine";
    case 80: return "Windy";
    case 81: return "Dry";
    case 82: return "Humid";
    case 83: return "Fog";
    case 84: return "Mist";
    case 85: return "Haze";
    case 90: return "Hot";
    case 91: return "Warm";
    case 92: return "Cool";
    case 93: return "Cold";
    default: return "Weather";
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
    /* Expect ...T23:02:00+08:00 */
    const char *t = strchr(iso, 'T');
    if (t && strlen(t) >= 6) {
        snprintf(dst, dst_len, "%.5s", t + 1); /* HH:MM */
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

static esp_err_t parse_weather_json(const char *json, weather_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(*out));
    out->humidity = -1;
    out->icon = -1;
    out->temp_valid = false;

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
        strncpy(out->condition, "Weather", sizeof(out->condition) - 1);
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

    http_accum_t acc = {
        .buf = buf,
        .len = 0,
        .cap = WEATHER_HTTP_BUF_SIZE,
    };
    buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = WEATHER_URL,
        .event_handler = http_event_handler,
        .user_data = &acc,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(buf);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
        free(buf);
        return err;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP status %d", status_code);
        free(buf);
        return ESP_FAIL;
    }
    if (acc.len == 0) {
        ESP_LOGE(TAG, "empty body");
        free(buf);
        return ESP_FAIL;
    }

    err = parse_weather_json(buf, out);
    free(buf);
    return err;
}