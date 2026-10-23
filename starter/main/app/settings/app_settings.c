#include "app_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "app_settings";

#define NVS_NS "upd_cfg"

#define KEY_WX_INT  "wx_int"
#define KEY_WX_PS   "wx_ps"
#define KEY_WX_PE   "wx_pe"
#define KEY_LRT_INT "lrt_int"
#define KEY_LRT_PS  "lrt_ps"
#define KEY_LRT_PE  "lrt_pe"
#define KEY_MQTT_H  "mqtt_h"
#define KEY_MQTT_P  "mqtt_p"

#define INTERVAL_MIN_LO 1
#define INTERVAL_MIN_HI 180

static app_settings_t s_cfg;
static bool s_ready;

static uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static uint8_t clamp_hour(uint8_t h)
{
    if (h > 24) {
        return 24;
    }
    return h;
}

static void trim_host(char *s)
{
    if (s == NULL) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) {
        i++;
    }
    if (i > 0) {
        memmove(s, s + i, strlen(s + i) + 1);
    }
    /* Strip accidental scheme prefix */
    if (strncmp(s, "mqtt://", 7) == 0) {
        memmove(s, s + 7, strlen(s + 7) + 1);
    }
    /* Drop trailing :port if user pasted host:port into host field */
    char *colon = strrchr(s, ':');
    if (colon && colon != s) {
        bool all_digit = true;
        for (char *p = colon + 1; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digit = false;
                break;
            }
        }
        if (all_digit && colon[1]) {
            *colon = '\0';
        }
    }
}

static void sanitize(app_settings_t *s)
{
    s->weather.interval_min = clamp_u16(s->weather.interval_min, INTERVAL_MIN_LO, INTERVAL_MIN_HI);
    s->lrt.interval_min = clamp_u16(s->lrt.interval_min, INTERVAL_MIN_LO, INTERVAL_MIN_HI);
    s->weather.period_start_h = clamp_hour(s->weather.period_start_h);
    s->weather.period_end_h = clamp_hour(s->weather.period_end_h);
    s->lrt.period_start_h = clamp_hour(s->lrt.period_start_h);
    s->lrt.period_end_h = clamp_hour(s->lrt.period_end_h);
    s->mqtt_host[APP_MQTT_HOST_MAX - 1] = '\0';
    trim_host(s->mqtt_host);
    if (s->mqtt_port == 0) {
        s->mqtt_port = 1883;
    }
    if (s->mqtt_port < 1) {
        s->mqtt_port = 1883;
    }
}

void app_settings_get_defaults(app_settings_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->weather.interval_min = 15;
    out->weather.period_start_h = 0;
    out->weather.period_end_h = 24;
    out->lrt.interval_min = 1;
    out->lrt.period_start_h = 0;
    out->lrt.period_end_h = 24;
    out->mqtt_host[0] = '\0';
    out->mqtt_port = 1883;
}

esp_err_t app_settings_init(void)
{
    app_settings_get_defaults(&s_cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no NVS yet; using defaults");
        s_ready = true;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RO failed: %s", esp_err_to_name(err));
        s_ready = true;
        return err;
    }

    uint16_t u16 = 0;
    uint8_t u8 = 0;
    if (nvs_get_u16(h, KEY_WX_INT, &u16) == ESP_OK) {
        s_cfg.weather.interval_min = u16;
    }
    if (nvs_get_u8(h, KEY_WX_PS, &u8) == ESP_OK) {
        s_cfg.weather.period_start_h = u8;
    }
    if (nvs_get_u8(h, KEY_WX_PE, &u8) == ESP_OK) {
        s_cfg.weather.period_end_h = u8;
    }
    if (nvs_get_u16(h, KEY_LRT_INT, &u16) == ESP_OK) {
        s_cfg.lrt.interval_min = u16;
    }
    if (nvs_get_u8(h, KEY_LRT_PS, &u8) == ESP_OK) {
        s_cfg.lrt.period_start_h = u8;
    }
    if (nvs_get_u8(h, KEY_LRT_PE, &u8) == ESP_OK) {
        s_cfg.lrt.period_end_h = u8;
    }
    size_t host_len = sizeof(s_cfg.mqtt_host);
    if (nvs_get_str(h, KEY_MQTT_H, s_cfg.mqtt_host, &host_len) != ESP_OK) {
        s_cfg.mqtt_host[0] = '\0';
    }
    if (nvs_get_u16(h, KEY_MQTT_P, &u16) == ESP_OK) {
        s_cfg.mqtt_port = u16;
    }
    nvs_close(h);

    sanitize(&s_cfg);
    s_ready = true;
    ESP_LOGI(TAG,
             "loaded wx int=%u period=%u-%u; lrt int=%u period=%u-%u; mqtt=%s:%u",
             (unsigned)s_cfg.weather.interval_min,
             (unsigned)s_cfg.weather.period_start_h,
             (unsigned)s_cfg.weather.period_end_h,
             (unsigned)s_cfg.lrt.interval_min,
             (unsigned)s_cfg.lrt.period_start_h,
             (unsigned)s_cfg.lrt.period_end_h,
             s_cfg.mqtt_host[0] ? s_cfg.mqtt_host : "(empty)",
             (unsigned)s_cfg.mqtt_port);
    return ESP_OK;
}

const app_settings_t *app_settings_get(void)
{
    if (!s_ready) {
        app_settings_get_defaults(&s_cfg);
        s_ready = true;
    }
    return &s_cfg;
}

esp_err_t app_settings_save(const app_settings_t *in)
{
    if (in == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    app_settings_t tmp = *in;
    sanitize(&tmp);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open RW failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u16(h, KEY_WX_INT, tmp.weather.interval_min);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_WX_PS, tmp.weather.period_start_h);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_WX_PE, tmp.weather.period_end_h);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, KEY_LRT_INT, tmp.lrt.interval_min);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_LRT_PS, tmp.lrt.period_start_h);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_LRT_PE, tmp.lrt.period_end_h);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_MQTT_H, tmp.mqtt_host);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, KEY_MQTT_P, tmp.mqtt_port);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return err;
    }

    s_cfg = tmp;
    s_ready = true;
    ESP_LOGI(TAG,
             "saved wx int=%u period=%u-%u; lrt int=%u period=%u-%u; mqtt=%s:%u",
             (unsigned)s_cfg.weather.interval_min,
             (unsigned)s_cfg.weather.period_start_h,
             (unsigned)s_cfg.weather.period_end_h,
             (unsigned)s_cfg.lrt.interval_min,
             (unsigned)s_cfg.lrt.period_start_h,
             (unsigned)s_cfg.lrt.period_end_h,
             s_cfg.mqtt_host[0] ? s_cfg.mqtt_host : "(empty)",
             (unsigned)s_cfg.mqtt_port);
    return ESP_OK;
}

bool app_settings_in_period(const app_update_schedule_t *sched)
{
    if (sched == NULL) {
        return true;
    }
    if (sched->period_start_h == sched->period_end_h) {
        return true;
    }

    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);
    int hour = ti.tm_hour;
    if (ti.tm_year < (2020 - 1900)) {
        return true;
    }

    int start = (int)sched->period_start_h;
    int end = (int)sched->period_end_h;
    if (start < end) {
        return hour >= start && hour < end;
    }
    return hour >= start || hour < end;
}

uint32_t app_settings_interval_ms(const app_update_schedule_t *sched)
{
    uint16_t mins = 15;
    if (sched) {
        mins = clamp_u16(sched->interval_min, INTERVAL_MIN_LO, INTERVAL_MIN_HI);
    }
    return (uint32_t)mins * 60U * 1000U;
}
