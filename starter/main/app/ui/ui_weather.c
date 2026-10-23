#include "ui_weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_icons.h"
#include "ha_mqtt.h"
#include "weather.h"
#include "wifi_manager.h"
#include "wx_icons.h"
#include "fonts.h"

static const char *TAG = "ui_weather";

#define HOURLY_SLOTS WEATHER_HOURLY_MAX

static lv_obj_t *s_root;
static lv_obj_t *s_place;
static lv_obj_t *s_temp;
static lv_obj_t *s_temp_icon;
static lv_obj_t *s_icon;
static lv_obj_t *s_condition;
static lv_obj_t *s_humidity;
static lv_obj_t *s_pressure;
static lv_obj_t *s_wind;
static lv_obj_t *s_updated;
static lv_obj_t *s_status;
static lv_obj_t *s_hourly_time[HOURLY_SLOTS];
static lv_obj_t *s_hourly_icon[HOURLY_SLOTS];
static lv_obj_t *s_hourly_temp[HOURLY_SLOTS];
static volatile bool s_fetching;
static bool s_have_data;
static bool s_mqtt_hooked;

static void label_set_text_if_changed(lv_obj_t *lbl, const char *text)
{
    if (lbl == NULL || text == NULL) {
        return;
    }
    const char *cur = lv_label_get_text(lbl);
    if (cur != NULL && strcmp(cur, text) == 0) {
        return;
    }
    lv_label_set_text(lbl, text);
}

static void set_updated_at_now(const weather_data_t *w)
{
    if (s_updated == NULL) {
        return;
    }
    char buf[40];
    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year >= (2020 - 1900)) {
        snprintf(buf, sizeof(buf), "Updated at %02d:%02d", ti.tm_hour, ti.tm_min);
    } else if (w != NULL && w->update_hhmm[0]) {
        snprintf(buf, sizeof(buf), "Updated at %s", w->update_hhmm);
    } else {
        snprintf(buf, sizeof(buf), "Updated at --:--");
    }
    label_set_text_if_changed(s_updated, buf);
}

static void set_status_lbl(const char *text)
{
    if (s_status && text) {
        label_set_text_if_changed(s_status, text);
    }
}

static lv_obj_t *make_icon_text_row(lv_obj_t *parent, const lv_image_dsc_t *img,
                                    uint32_t recolor, lv_obj_t **out_label,
                                    const char *initial)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_SIZE_CONTENT);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = lv_image_create(row);
    lv_image_set_src(ic, img);
    lv_obj_set_size(ic, 24, 24);
    lv_obj_set_style_image_recolor(ic, lv_color_hex(recolor), 0);
    lv_obj_set_style_image_recolor_opa(ic, LV_OPA_COVER, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, initial);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(recolor), 0);
    *out_label = lbl;
    return row;
}


static bool hour_is_night(int hour)
{
    /* HK civil night heuristic when sunrise/sunset unused on HA path. */
    return hour < 6 || hour >= 18;
}

static bool local_is_night_now(void)
{
    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year < (2020 - 1900)) {
        return false;
    }
    return hour_is_night(ti.tm_hour);
}

static bool hhmm_is_night(const char *hhmm)
{
    if (hhmm == NULL || hhmm[0] == '\0') {
        return local_is_night_now();
    }
    int h = 0, m = 0;
    if (sscanf(hhmm, "%d:%d", &h, &m) < 1) {
        return local_is_night_now();
    }
    (void)m;
    return hour_is_night(h);
}

static void apply_hourly(const weather_data_t *w)
{
    for (int i = 0; i < HOURLY_SLOTS; i++) {
        if (s_hourly_time[i] == NULL) {
            continue;
        }
        if (w == NULL || i >= w->hourly_count) {
            label_set_text_if_changed(s_hourly_time[i], "--:--");
            label_set_text_if_changed(s_hourly_temp[i], "--");
            if (s_hourly_icon[i]) {
                lv_image_set_src(s_hourly_icon[i], &wx_icon_generic);
                lv_obj_set_style_image_recolor(s_hourly_icon[i], lv_color_hex(0x6b7280), 0);
            }
            continue;
        }
        const weather_hourly_t *h = &w->hourly[i];
        label_set_text_if_changed(s_hourly_time[i],
                                  h->time_hhmm[0] ? h->time_hhmm : "--:--");
        if (h->temp_valid) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f°", (double)h->temp_c);
            label_set_text_if_changed(s_hourly_temp[i], buf);
        } else {
            label_set_text_if_changed(s_hourly_temp[i], "--");
        }
        if (s_hourly_icon[i]) {
            wx_icon_style_t st = wx_icon_style_for_ha(h->condition_en, hhmm_is_night(h->time_hhmm));
            lv_image_set_src(s_hourly_icon[i], st.img);
            lv_obj_set_style_image_recolor(s_hourly_icon[i], lv_color_hex(st.recolor), 0);
            lv_obj_set_style_image_recolor_opa(s_hourly_icon[i], LV_OPA_COVER, 0);
        }
    }
}

static void apply_weather(const weather_data_t *w)
{
    if (w == NULL) {
        return;
    }
    s_have_data = true;
    s_fetching = false;

    if (s_place) {
        const char *place = w->place[0] ? w->place : "Home";
        label_set_text_if_changed(s_place, place);
    }

    if (s_temp) {
        if (w->temp_valid) {
            char buf[24];
            /* One decimal like HA card (24.5) */
            snprintf(buf, sizeof(buf), "%.1f", (double)w->temp_c);
            label_set_text_if_changed(s_temp, buf);
        } else {
            label_set_text_if_changed(s_temp, "--");
        }
    }

    if (s_icon) {
        wx_icon_style_t st = wx_icon_style_for_ha(w->condition_en, local_is_night_now());
        lv_image_set_src(s_icon, st.img);
        lv_obj_set_style_image_recolor(s_icon, lv_color_hex(st.recolor), 0);
        lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);
    }

    if (s_condition) {
        const char *cond = w->condition[0] ? w->condition : "天氣";
        label_set_text_if_changed(s_condition, cond);
    }

    if (s_humidity) {
        if (w->humidity >= 0) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%d%%", w->humidity);
            label_set_text_if_changed(s_humidity, buf);
        } else {
            label_set_text_if_changed(s_humidity, "--");
        }
    }

    if (s_pressure) {
        if (w->pressure_hpa >= 0.0f) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f hPa", (double)w->pressure_hpa);
            label_set_text_if_changed(s_pressure, buf);
        } else {
            label_set_text_if_changed(s_pressure, "-- hPa");
        }
    }

    if (s_wind) {
        if (w->wind_kmh >= 0.0f) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f km/h", (double)w->wind_kmh);
            label_set_text_if_changed(s_wind, buf);
        } else {
            label_set_text_if_changed(s_wind, "-- km/h");
        }
    }

    apply_hourly(w);
    set_updated_at_now(w);
    set_status_lbl("HA weather (MQTT)");
}

typedef struct {
    weather_data_t data;
} mqtt_ui_msg_t;

static void mqtt_apply_ui(void *param)
{
    mqtt_ui_msg_t *msg = (mqtt_ui_msg_t *)param;
    if (msg) {
        apply_weather(&msg->data);
        free(msg);
    }
}

static void on_mqtt_weather(const weather_data_t *data, void *user)
{
    (void)user;
    if (data == NULL) {
        return;
    }
    mqtt_ui_msg_t *msg = heap_caps_malloc(sizeof(*msg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (msg == NULL) {
        msg = malloc(sizeof(*msg));
    }
    if (msg == NULL) {
        return;
    }
    memcpy(&msg->data, data, sizeof(*data));
    if (esp_lv_adapter_lock(500) == ESP_OK) {
        mqtt_apply_ui(msg);
        esp_lv_adapter_unlock();
    } else {
        free(msg);
    }
}

static void start_mqtt_and_refresh(const char *pending_status)
{
    if (!wifi_manager_is_connected()) {
        set_status_lbl("WiFi not connected");
        return;
    }
    if (!s_mqtt_hooked) {
        ha_mqtt_set_weather_cb(on_mqtt_weather, NULL);
        s_mqtt_hooked = true;
    }
    esp_err_t err = ha_mqtt_start();
    if (err == ESP_ERR_INVALID_ARG) {
        set_status_lbl("Set MQTT broker IP in Settings");
        return;
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        set_status_lbl("MQTT start failed");
        ESP_LOGW(TAG, "ha_mqtt_start: %s", esp_err_to_name(err));
        return;
    }
    if (pending_status) {
        set_status_lbl(pending_status);
    }
    s_fetching = true;
    err = ha_mqtt_request_refresh();
    if (err != ESP_OK) {
        /* Retained message may still arrive on subscribe; not fatal. */
        ESP_LOGI(TAG, "refresh publish pending (%s); waiting retained",
                 esp_err_to_name(err));
        if (!s_have_data) {
            set_status_lbl("Waiting MQTT weather...");
        }
        s_fetching = false;
    }
}

static void place_tap_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    start_mqtt_and_refresh("Refreshing weather...");
}

void ui_weather_apply_settings(void)
{
    /* Weather poll interval/period removed — HA MQTT retain/automation owns cadence.
     * Settings save only retargets broker IP/port. */
    if (wifi_manager_is_connected()) {
        esp_err_t e = ha_mqtt_apply_settings();
        ESP_LOGI(TAG, "mqtt apply_settings: %s", esp_err_to_name(e));
        start_mqtt_and_refresh(NULL);
    }
}

void ui_weather_request_refresh(void)
{
    start_mqtt_and_refresh(s_have_data ? NULL : "Loading weather...");
}

void ui_weather_set_status_unlocked(const char *text)
{
    set_status_lbl(text);
}

void ui_weather_set_status(const char *text)
{
    if (text == NULL) {
        return;
    }
    if (esp_lv_adapter_lock(100) == ESP_OK) {
        set_status_lbl(text);
        esp_lv_adapter_unlock();
    }
}

void ui_weather_create(lv_obj_t *parent)
{
    s_root = parent;
    s_fetching = false;
    s_have_data = false;
    s_mqtt_hooked = false;
    memset(s_hourly_time, 0, sizeof(s_hourly_time));
    memset(s_hourly_icon, 0, sizeof(s_hourly_icon));
    memset(s_hourly_temp, 0, sizeof(s_hourly_temp));

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0e0e18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 18, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 36);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    s_place = lv_label_create(hdr);
    lv_label_set_text(s_place, "Home");
    lv_obj_set_style_text_font(s_place, font_tc_16(), 0);
    lv_obj_set_style_text_color(s_place, lv_color_hex(0x93c5fd), 0);
    lv_obj_align(s_place, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_flag(s_place, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_place, place_tap_cb, LV_EVENT_CLICKED, NULL);

    /* Meta: left temp+condition | right humidity/pressure/wind (HA card) */
    lv_obj_t *meta_row = lv_obj_create(parent);
    lv_obj_set_width(meta_row, LV_PCT(100));
    lv_obj_set_height(meta_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta_row, 0, 0);
    lv_obj_set_style_pad_all(meta_row, 0, 0);
    lv_obj_set_flex_flow(meta_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta_row, 8, 0);
    lv_obj_clear_flag(meta_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left_box = lv_obj_create(meta_row);
    lv_obj_set_height(left_box, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(left_box, 0, 0);
    lv_obj_set_flex_grow(left_box, 1);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(left_box, 6, 0);
    lv_obj_clear_flag(left_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *temp_box = lv_obj_create(left_box);
    lv_obj_set_width(temp_box, LV_PCT(100));
    lv_obj_set_height(temp_box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(temp_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(temp_box, 0, 0);
    lv_obj_set_style_pad_all(temp_box, 0, 0);
    lv_obj_set_flex_flow(temp_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(temp_box, 6, 0);
    lv_obj_clear_flag(temp_box, LV_OBJ_FLAG_SCROLLABLE);

    s_temp = lv_label_create(temp_box);
    label_set_text_if_changed(s_temp, "--");
    lv_obj_set_style_text_font(s_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_temp, lv_color_hex(0xf0f4ff), 0);

    s_temp_icon = lv_image_create(temp_box);
    lv_image_set_src(s_temp_icon, &icon_temp_c);
    lv_obj_set_size(s_temp_icon, 40, 40);
    lv_obj_set_style_image_recolor(s_temp_icon, lv_color_hex(0xf0f4ff), 0);
    lv_obj_set_style_image_recolor_opa(s_temp_icon, LV_OPA_COVER, 0);

    lv_obj_t *cond_row = lv_obj_create(left_box);
    lv_obj_set_width(cond_row, LV_PCT(100));
    lv_obj_set_height(cond_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cond_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cond_row, 0, 0);
    lv_obj_set_style_pad_all(cond_row, 0, 0);
    lv_obj_set_flex_flow(cond_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cond_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cond_row, 8, 0);
    lv_obj_clear_flag(cond_row, LV_OBJ_FLAG_SCROLLABLE);

    s_icon = lv_image_create(cond_row);
    lv_image_set_src(s_icon, &wx_icon_generic);
    lv_obj_set_style_image_recolor(s_icon, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);
    lv_obj_set_size(s_icon, 32, 32);

    s_condition = lv_label_create(cond_row);
    lv_label_set_text(s_condition, "撳地點更新");
    lv_obj_set_style_text_font(s_condition, font_tc_20(), 0);
    lv_obj_set_style_text_color(s_condition, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_flex_grow(s_condition, 1);
    lv_obj_set_style_min_width(s_condition, 0, 0);
    lv_label_set_long_mode(s_condition, LV_LABEL_LONG_CLIP);

    lv_obj_t *detail_box = lv_obj_create(meta_row);
    lv_obj_set_width(detail_box, LV_SIZE_CONTENT);
    lv_obj_set_height(detail_box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(detail_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detail_box, 0, 0);
    lv_obj_set_style_pad_all(detail_box, 0, 0);
    lv_obj_set_flex_flow(detail_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(detail_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(detail_box, 4, 0);
    lv_obj_clear_flag(detail_box, LV_OBJ_FLAG_SCROLLABLE);

    make_icon_text_row(detail_box, &icon_rain_drop, 0x38bdf8, &s_humidity, "--");
    make_icon_text_row(detail_box, &wx_icon_generic, 0xa78bfa, &s_pressure, "-- hPa");
    make_icon_text_row(detail_box, &wx_icon_windy, 0x67e8f9, &s_wind, "-- km/h");

    /* Hourly strip: 4 slots (HA card style) */
    lv_obj_t *hourly = lv_obj_create(parent);
    lv_obj_set_width(hourly, LV_PCT(100));
    lv_obj_set_height(hourly, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(hourly, lv_color_hex(0x161622), 0);
    lv_obj_set_style_bg_opa(hourly, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hourly, 10, 0);
    lv_obj_set_style_border_width(hourly, 0, 0);
    lv_obj_set_style_pad_all(hourly, 10, 0);
    lv_obj_set_flex_flow(hourly, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hourly, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hourly, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < HOURLY_SLOTS; i++) {
        lv_obj_t *col = lv_obj_create(hourly);
        lv_obj_set_width(col, LV_SIZE_CONTENT);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 4, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        s_hourly_time[i] = lv_label_create(col);
        lv_label_set_text(s_hourly_time[i], "--:--");
        lv_obj_set_style_text_font(s_hourly_time[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_hourly_time[i], lv_color_hex(0x9ca3af), 0);

        s_hourly_icon[i] = lv_image_create(col);
        lv_image_set_src(s_hourly_icon[i], &wx_icon_generic);
        lv_obj_set_size(s_hourly_icon[i], 28, 28);
        lv_obj_set_style_image_recolor(s_hourly_icon[i], lv_color_hex(0x6b7280), 0);
        lv_obj_set_style_image_recolor_opa(s_hourly_icon[i], LV_OPA_COVER, 0);

        s_hourly_temp[i] = lv_label_create(col);
        lv_label_set_text(s_hourly_temp[i], "--");
        lv_obj_set_style_text_font(s_hourly_temp[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_hourly_temp[i], lv_color_hex(0xe5e7eb), 0);
    }

    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *footer = lv_obj_create(parent);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(footer, 4, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(footer);
    lv_label_set_text(s_status, "Starting...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x6b7280), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);

    s_updated = lv_label_create(footer);
    lv_label_set_text(s_updated, "Updated at --:--");
    lv_obj_set_style_text_font(s_updated, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_updated, lv_color_hex(0x6b7280), 0);
    lv_obj_set_style_text_align(s_updated, LV_TEXT_ALIGN_CENTER, 0);

    ESP_LOGI(TAG, "HA MQTT weather panel ready (hourly=%d; no local poll timer)", HOURLY_SLOTS);
}
