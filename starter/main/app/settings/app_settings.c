#include "app_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lrt_stations.h"

static const char *TAG = "app_settings";

#define NVS_NS "upd_cfg"

#define KEY_LRT_INT "lrt_int"
#define KEY_LRT_PS  "lrt_ps"
#define KEY_LRT_PE  "lrt_pe"
#define KEY_MQTT_H  "mqtt_h"
#define KEY_MQTT_P  "mqtt_p"
#define KEY_LRT_ST  "lrt_st"
#define KEY_LRT_PINS "lrt_pins"
#define KEY_IDLE_MIN "idle_min"

#define INTERVAL_MIN_LO 1
#define INTERVAL_MIN_HI 180
#define IDLE_MIN_LO 0
#define IDLE_MIN_HI 180

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

/** Drop invalid / duplicate pins and pack non-zero IDs to the front. */
static void sanitize_pins(uint16_t pins[APP_LRT_PIN_MAX])
{
    uint16_t packed[APP_LRT_PIN_MAX];
    memset(packed, 0, sizeof(packed));
    int n = 0;
    for (int i = 0; i < APP_LRT_PIN_MAX; i++) {
        uint16_t id = pins[i];
        if (id == 0 || !lrt_station_id_valid((int)id)) {
            continue;
        }
        bool dup = false;
        for (int j = 0; j < n; j++) {
            if (packed[j] == id) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        packed[n++] = id;
    }
    memcpy(pins, packed, sizeof(packed));
}

static void sanitize(app_settings_t *s)
{
    s->lrt.interval_min = clamp_u16(s->lrt.interval_min, INTERVAL_MIN_LO, INTERVAL_MIN_HI);
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
    if (!lrt_station_id_valid((int)s->lrt_station_id)) {
        s->lrt_station_id = (uint16_t)LRT_STATION_DEFAULT;
    }
    sanitize_pins(s->lrt_pinned);
    s->display_idle_min = clamp_u16(s->display_idle_min, IDLE_MIN_LO, IDLE_MIN_HI);
}

void app_settings_get_defaults(app_settings_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->lrt.interval_min = 1;
    out->lrt.period_start_h = 0;
    out->lrt.period_end_h = 24;
    out->lrt_station_id = (uint16_t)LRT_STATION_DEFAULT;
    memset(out->lrt_pinned, 0, sizeof(out->lrt_pinned));
    out->mqtt_host[0] = '\0';
    out->mqtt_port = 1883;
    out->display_idle_min = 10;
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
    if (nvs_get_u16(h, KEY_LRT_ST, &u16) == ESP_OK) {
        s_cfg.lrt_station_id = u16;
    }
    if (nvs_get_u16(h, KEY_IDLE_MIN, &u16) == ESP_OK) {
        s_cfg.display_idle_min = u16;
    }
    {
        uint16_t pins[APP_LRT_PIN_MAX];
        memset(pins, 0, sizeof(pins));
        size_t len = sizeof(pins);
        if (nvs_get_blob(h, KEY_LRT_PINS, pins, &len) == ESP_OK && len >= sizeof(uint16_t)) {
            size_t n = len / sizeof(uint16_t);
            if (n > APP_LRT_PIN_MAX) {
                n = APP_LRT_PIN_MAX;
            }
            memcpy(s_cfg.lrt_pinned, pins, n * sizeof(uint16_t));
        }
    }
    nvs_close(h);

    sanitize(&s_cfg);
    s_ready = true;
    ESP_LOGI(TAG,
             "loaded lrt int=%u period=%u-%u station=%u pins=%u,%u,%u; mqtt=%s:%u; idle=%u min",
             (unsigned)s_cfg.lrt.interval_min,
             (unsigned)s_cfg.lrt.period_start_h,
             (unsigned)s_cfg.lrt.period_end_h,
             (unsigned)s_cfg.lrt_station_id,
             (unsigned)s_cfg.lrt_pinned[0],
             (unsigned)s_cfg.lrt_pinned[1],
             (unsigned)s_cfg.lrt_pinned[2],
             s_cfg.mqtt_host[0] ? s_cfg.mqtt_host : "(empty)",
             (unsigned)s_cfg.mqtt_port,
             (unsigned)s_cfg.display_idle_min);
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

    /* Drop obsolete weather schedule keys (HA MQTT owns cadence now). */
    (void)nvs_erase_key(h, "wx_int");
    (void)nvs_erase_key(h, "wx_ps");
    (void)nvs_erase_key(h, "wx_pe");

    err = nvs_set_u16(h, KEY_LRT_INT, tmp.lrt.interval_min);
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
        err = nvs_set_u16(h, KEY_LRT_ST, tmp.lrt_station_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_LRT_PINS, tmp.lrt_pinned, sizeof(tmp.lrt_pinned));
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, KEY_IDLE_MIN, tmp.display_idle_min);
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
             "saved lrt int=%u period=%u-%u station=%u pins=%u,%u,%u; mqtt=%s:%u; idle=%u min",
             (unsigned)s_cfg.lrt.interval_min,
             (unsigned)s_cfg.lrt.period_start_h,
             (unsigned)s_cfg.lrt.period_end_h,
             (unsigned)s_cfg.lrt_station_id,
             (unsigned)s_cfg.lrt_pinned[0],
             (unsigned)s_cfg.lrt_pinned[1],
             (unsigned)s_cfg.lrt_pinned[2],
             s_cfg.mqtt_host[0] ? s_cfg.mqtt_host : "(empty)",
             (unsigned)s_cfg.mqtt_port,
             (unsigned)s_cfg.display_idle_min);
    return ESP_OK;
}

bool app_settings_is_lrt_pinned(uint16_t station_id)
{
    if (station_id == 0) {
        return false;
    }
    const app_settings_t *cfg = app_settings_get();
    for (int i = 0; i < APP_LRT_PIN_MAX; i++) {
        if (cfg->lrt_pinned[i] == station_id) {
            return true;
        }
    }
    return false;
}

int app_settings_lrt_pin_count(void)
{
    const app_settings_t *cfg = app_settings_get();
    int n = 0;
    for (int i = 0; i < APP_LRT_PIN_MAX; i++) {
        if (cfg->lrt_pinned[i] != 0) {
            n++;
        }
    }
    return n;
}

esp_err_t app_settings_pin_lrt_station(uint16_t station_id)
{
    if (!lrt_station_id_valid((int)station_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_settings_is_lrt_pinned(station_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (app_settings_lrt_pin_count() >= APP_LRT_PIN_MAX) {
        /* Block - do not replace oldest. UI shows status. */
        return ESP_ERR_NO_MEM;
    }

    app_settings_t cfg = *app_settings_get();
    for (int i = 0; i < APP_LRT_PIN_MAX; i++) {
        if (cfg.lrt_pinned[i] == 0) {
            cfg.lrt_pinned[i] = station_id;
            return app_settings_save(&cfg);
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t app_settings_unpin_lrt_station(uint16_t station_id)
{
    if (station_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    app_settings_t cfg = *app_settings_get();
    bool found = false;
    for (int i = 0; i < APP_LRT_PIN_MAX; i++) {
        if (cfg.lrt_pinned[i] == station_id) {
            cfg.lrt_pinned[i] = 0;
            found = true;
        }
    }
    if (!found) {
        return ESP_OK;
    }
    sanitize_pins(cfg.lrt_pinned);
    return app_settings_save(&cfg);
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

uint32_t app_settings_display_idle_ms(void)
{
    const app_settings_t *cfg = app_settings_get();
    uint16_t mins = clamp_u16(cfg->display_idle_min, IDLE_MIN_LO, IDLE_MIN_HI);
    if (mins == 0) {
        return 0;
    }
    return (uint32_t)mins * 60U * 1000U;
}
