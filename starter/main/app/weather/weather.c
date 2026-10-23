#include "weather.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "weather";

const char *weather_icon_label(int icon)
{
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
    case 70: case 71: case 72: case 73: case 74: case 75: return "天色良好";
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

const char *weather_ha_state_label_zh(const char *state)
{
    if (state == NULL || state[0] == '\0') {
        return "天氣";
    }
    if (strcmp(state, "clear-night") == 0) return "晴夜";
    if (strcmp(state, "cloudy") == 0) return "陰";
    if (strcmp(state, "fog") == 0) return "霧";
    if (strcmp(state, "hail") == 0) return "冰雹";
    if (strcmp(state, "lightning") == 0) return "雷電";
    if (strcmp(state, "lightning-rainy") == 0) return "雷雨";
    if (strcmp(state, "partlycloudy") == 0) return "間中有雲";
    if (strcmp(state, "pouring") == 0) return "大雨";
    if (strcmp(state, "rainy") == 0) return "雨";
    if (strcmp(state, "snowy") == 0) return "雪";
    if (strcmp(state, "snowy-rainy") == 0) return "雨夾雪";
    if (strcmp(state, "sunny") == 0) return "晴朗";
    if (strcmp(state, "windy") == 0) return "大風";
    if (strcmp(state, "windy-variant") == 0) return "大風";
    if (strcmp(state, "exceptional") == 0) return "異常";
    return "天氣";
}

static void extract_hhmm(char *dst, size_t dst_len, const char *iso)
{
    if (dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (iso == NULL || iso[0] == '\0') {
        return;
    }
    if (iso[0] >= '0' && iso[0] <= '9' && iso[2] == ':' && strlen(iso) >= 5) {
        snprintf(dst, dst_len, "%.5s", iso);
        return;
    }
    const char *t = strchr(iso, 'T');
    if (t == NULL) {
        t = strchr(iso, ' ');
    }
    if (t != NULL && strlen(t) >= 6) {
        t++;
        if (t[0] >= '0' && t[0] <= '9' && t[2] == ':') {
            snprintf(dst, dst_len, "%.5s", t);
        }
    }
}

static void copy_str(char *dst, size_t n, cJSON *j)
{
    if (n == 0) {
        return;
    }
    dst[0] = '\0';
    if (cJSON_IsString(j) && j->valuestring) {
        snprintf(dst, n, "%s", j->valuestring);
    }
}


esp_err_t weather_parse_ha_mqtt_json(const char *json, weather_data_t *out)
{
    if (json == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(*out));
    out->humidity = -1;
    out->pressure_hpa = -1.0f;
    out->wind_kmh = -1.0f;
    out->wind_bearing = -1;
    out->icon = -1;
    out->from_mqtt = true;

    copy_str(out->place, sizeof(out->place), cJSON_GetObjectItemCaseSensitive(root, "name"));
    if (out->place[0] == '\0') {
        snprintf(out->place, sizeof(out->place), "%s", "Home");
    }

    copy_str(out->condition_en, sizeof(out->condition_en),
             cJSON_GetObjectItemCaseSensitive(root, "state"));
    copy_str(out->condition, sizeof(out->condition),
             cJSON_GetObjectItemCaseSensitive(root, "state_zh"));
    if (out->condition[0] == '\0') {
        snprintf(out->condition, sizeof(out->condition), "%s",
                 weather_ha_state_label_zh(out->condition_en));
    }

    cJSON *temp = cJSON_GetObjectItemCaseSensitive(root, "temperature");
    if (cJSON_IsNumber(temp)) {
        out->temp_c = (float)temp->valuedouble;
        out->temp_valid = true;
    }

    cJSON *hum = cJSON_GetObjectItemCaseSensitive(root, "humidity");
    if (cJSON_IsNumber(hum)) {
        out->humidity = hum->valueint;
    }

    cJSON *pres = cJSON_GetObjectItemCaseSensitive(root, "pressure");
    if (cJSON_IsNumber(pres)) {
        out->pressure_hpa = (float)pres->valuedouble;
    }

    cJSON *wind = cJSON_GetObjectItemCaseSensitive(root, "wind_speed");
    if (cJSON_IsNumber(wind)) {
        out->wind_kmh = (float)wind->valuedouble;
    }

    cJSON *bearing = cJSON_GetObjectItemCaseSensitive(root, "wind_bearing");
    if (cJSON_IsNumber(bearing)) {
        out->wind_bearing = bearing->valueint;
    }

    cJSON *upd = cJSON_GetObjectItemCaseSensitive(root, "updated");
    if (cJSON_IsString(upd) && upd->valuestring) {
        extract_hhmm(out->update_hhmm, sizeof(out->update_hhmm), upd->valuestring);
    }

    cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (cJSON_IsArray(hourly)) {
        int n = cJSON_GetArraySize(hourly);
        if (n > WEATHER_HOURLY_MAX) {
            n = WEATHER_HOURLY_MAX;
        }
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(hourly, i);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            weather_hourly_t *h = &out->hourly[out->hourly_count];
            h->precip_prob = -1;
            copy_str(h->time_hhmm, sizeof(h->time_hhmm),
                     cJSON_GetObjectItemCaseSensitive(item, "t"));
            if (h->time_hhmm[0] == '\0') {
                extract_hhmm(h->time_hhmm, sizeof(h->time_hhmm),
                             cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "datetime"))
                                 ? cJSON_GetObjectItemCaseSensitive(item, "datetime")->valuestring
                                 : NULL);
            }
            copy_str(h->condition_en, sizeof(h->condition_en),
                     cJSON_GetObjectItemCaseSensitive(item, "c"));
            if (h->condition_en[0] == '\0') {
                copy_str(h->condition_en, sizeof(h->condition_en),
                         cJSON_GetObjectItemCaseSensitive(item, "condition"));
            }
            copy_str(h->condition_zh, sizeof(h->condition_zh),
                     cJSON_GetObjectItemCaseSensitive(item, "zh"));
            if (h->condition_zh[0] == '\0') {
                snprintf(h->condition_zh, sizeof(h->condition_zh), "%s",
                         weather_ha_state_label_zh(h->condition_en));
            }
            cJSON *ht = cJSON_GetObjectItemCaseSensitive(item, "temp");
            if (!cJSON_IsNumber(ht)) {
                ht = cJSON_GetObjectItemCaseSensitive(item, "temperature");
            }
            if (cJSON_IsNumber(ht)) {
                h->temp_c = (float)ht->valuedouble;
                h->temp_valid = true;
            }
            cJSON *pp = cJSON_GetObjectItemCaseSensitive(item, "pp");
            if (!cJSON_IsNumber(pp)) {
                pp = cJSON_GetObjectItemCaseSensitive(item, "precipitation_probability");
            }
            if (cJSON_IsNumber(pp)) {
                h->precip_prob = pp->valueint;
            }
            out->hourly_count++;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "HA mqtt place=%s state=%s temp=%.1f rh=%d hourly=%d",
             out->place, out->condition_en,
             out->temp_valid ? out->temp_c : -999.0f,
             out->humidity, out->hourly_count);
    return out->temp_valid || out->condition_en[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
}
