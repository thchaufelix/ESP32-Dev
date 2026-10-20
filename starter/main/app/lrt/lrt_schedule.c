#include "lrt_schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "wifi_manager.h"

static const char *TAG = "lrt_sched";

#define LRT_HTTP_BUF_SIZE  (12 * 1024)
#define LRT_URL_MAX        160

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

static void copy_json_str(char *dst, size_t dst_len, cJSON *obj, const char *key)
{
    if (dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dst_len - 1);
        dst[dst_len - 1] = '\0';
    }
}

static esp_err_t parse_schedule_json(const char *json, lrt_schedule_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    out->api_status = cJSON_IsNumber(status) ? status->valueint : -1;
    copy_json_str(out->system_time, sizeof(out->system_time), root, "system_time");

    cJSON *platforms = cJSON_GetObjectItemCaseSensitive(root, "platform_list");
    if (!cJSON_IsArray(platforms)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "missing platform_list");
        return ESP_FAIL;
    }

    cJSON *plat;
    cJSON_ArrayForEach(plat, platforms) {
        if (out->count >= LRT_MAX_ENTRIES) {
            break;
        }
        cJSON *pid = cJSON_GetObjectItemCaseSensitive(plat, "platform_id");
        int platform_id = cJSON_IsNumber(pid) ? pid->valueint : 0;

        cJSON *routes = cJSON_GetObjectItemCaseSensitive(plat, "route_list");
        if (!cJSON_IsArray(routes)) {
            continue;
        }

        cJSON *route;
        cJSON_ArrayForEach(route, routes) {
            if (out->count >= LRT_MAX_ENTRIES) {
                break;
            }
            lrt_entry_t *e = &out->entries[out->count];
            memset(e, 0, sizeof(*e));
            e->platform_id = platform_id;
            cJSON *tl = cJSON_GetObjectItemCaseSensitive(route, "train_length");
            e->train_length = cJSON_IsNumber(tl) ? tl->valueint : 0;
            copy_json_str(e->route_no, sizeof(e->route_no), route, "route_no");
            copy_json_str(e->dest_ch, sizeof(e->dest_ch), route, "dest_ch");
            copy_json_str(e->dest_en, sizeof(e->dest_en), route, "dest_en");
            copy_json_str(e->time_ch, sizeof(e->time_ch), route, "time_ch");
            copy_json_str(e->time_en, sizeof(e->time_en), route, "time_en");
            if (e->time_en[0] == '\0' && e->time_ch[0] != '\0') {
                strncpy(e->time_en, e->time_ch, sizeof(e->time_en) - 1);
            }
            if (e->dest_en[0] == '\0' && e->dest_ch[0] != '\0') {
                strncpy(e->dest_en, e->dest_ch, sizeof(e->dest_en) - 1);
            }
            out->count++;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "parsed %u entries status=%d time=%s",
             (unsigned)out->count, out->api_status, out->system_time);
    return ESP_OK;
}

esp_err_t lrt_schedule_fetch(int station_id, lrt_schedule_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected");
        return ESP_ERR_INVALID_STATE;
    }

    char *buf = heap_caps_malloc(LRT_HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc(LRT_HTTP_BUF_SIZE);
    }
    if (buf == NULL) {
        ESP_LOGE(TAG, "alloc %d failed", LRT_HTTP_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    http_accum_t acc = {
        .buf = buf,
        .len = 0,
        .cap = LRT_HTTP_BUF_SIZE,
    };
    buf[0] = '\0';

    char url[LRT_URL_MAX];
    snprintf(url, sizeof(url),
             "https://rt.data.gov.hk/v1/transport/mtr/lrt/getSchedule?station_id=%d&with_special=1",
             station_id);

    esp_http_client_config_t cfg = {
        .url = url,
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

    err = parse_schedule_json(buf, out);
    free(buf);
    return err;
}
