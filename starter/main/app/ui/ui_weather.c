#include "ui_weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_icons.h"
#include "weather.h"
#include "wifi_manager.h"
#include "wx_icons.h"

static const char *TAG = "ui_weather";

#define WEATHER_PERIOD_MS  (15 * 60 * 1000)

static lv_obj_t *s_root;
static lv_obj_t *s_place;
static lv_obj_t *s_temp;
static lv_obj_t *s_temp_icon;
static lv_obj_t *s_icon;
static lv_obj_t *s_condition;
static lv_obj_t *s_humidity;
static lv_obj_t *s_sunrise;
static lv_obj_t *s_sunset;
static lv_obj_t *s_updated;
static lv_obj_t *s_status;
static lv_obj_t *s_refresh_btn;
static volatile bool s_fetching;
static bool s_have_data;

static void set_status_lbl(const char *text)
{
    if (s_status && text) {
        lv_label_set_text(s_status, text);
    }
}

static void apply_weather(const weather_data_t *w)
{
    if (w == NULL) {
        return;
    }
    s_have_data = true;

    if (s_place) {
        lv_label_set_text(s_place, w->place[0] ? w->place : "Tuen Mun");
    }

    if (s_temp) {
        if (w->temp_valid) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%d", w->temp_c);
            lv_label_set_text(s_temp, buf);
        } else {
            lv_label_set_text(s_temp, "--");
        }
    }

    if (s_icon) {
        wx_icon_style_t st = wx_icon_style_for_hko(w->icon);
        lv_image_set_src(s_icon, st.img);
        lv_obj_set_style_image_recolor(s_icon, lv_color_hex(st.recolor), 0);
        lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);
    }

    if (s_condition) {
        const char *cond = w->condition[0] ? w->condition : "Weather";
        lv_label_set_text(s_condition, cond);
    }

    if (s_humidity) {
        if (w->humidity >= 0) {
            char buf[40];
            snprintf(buf, sizeof(buf), "Humidity %d%% (HKO)", w->humidity);
            lv_label_set_text(s_humidity, buf);
        } else {
            lv_label_set_text(s_humidity, "Humidity --");
        }
    }

    if (s_sunrise) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Sunrise %s",
                 w->sunrise[0] ? w->sunrise : "--:--");
        lv_label_set_text(s_sunrise, buf);
    }
    if (s_sunset) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Sunset %s",
                 w->sunset[0] ? w->sunset : "--:--");
        lv_label_set_text(s_sunset, buf);
    }

    if (s_updated) {
        if (w->update_hhmm[0]) {
            char buf[40];
            snprintf(buf, sizeof(buf), "Updated %s", w->update_hhmm);
            lv_label_set_text(s_updated, buf);
        } else {
            lv_label_set_text(s_updated, "Updated --:--");
        }
    }
}

typedef struct {
    esp_err_t err;
    weather_data_t data;
} fetch_result_t;

static void fetch_done_ui(void *param)
{
    fetch_result_t *res = (fetch_result_t *)param;
    s_fetching = false;
    if (s_refresh_btn) {
        lv_obj_clear_state(s_refresh_btn, LV_STATE_DISABLED);
    }

    if (res == NULL) {
        set_status_lbl("Weather fetch error");
        return;
    }

    if (res->err == ESP_ERR_INVALID_STATE) {
        set_status_lbl("WiFi not connected");
    } else if (res->err == ESP_ERR_NOT_FOUND) {
        set_status_lbl("No Tuen Mun temp");
        apply_weather(&res->data);
    } else if (res->err != ESP_OK) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Weather: %s", esp_err_to_name(res->err));
        set_status_lbl(buf);
    } else {
        apply_weather(&res->data);
        set_status_lbl("HKO current weather");
    }
    free(res);
}

static void fetch_task(void *arg)
{
    (void)arg;
    fetch_result_t *res = calloc(1, sizeof(*res));
    if (res == NULL) {
        ESP_LOGE(TAG, "result alloc failed");
        s_fetching = false;
        if (esp_lv_adapter_lock(200) == ESP_OK) {
            set_status_lbl("Out of memory");
            if (s_refresh_btn) {
                lv_obj_clear_state(s_refresh_btn, LV_STATE_DISABLED);
            }
            esp_lv_adapter_unlock();
        }
        vTaskDelete(NULL);
        return;
    }

    res->err = weather_fetch(&res->data);
    ESP_LOGI(TAG, "fetch done err=%s", esp_err_to_name(res->err));

    if (esp_lv_adapter_lock(500) == ESP_OK) {
        fetch_done_ui(res);
        esp_lv_adapter_unlock();
    } else {
        ESP_LOGW(TAG, "LVGL lock timeout; drop result");
        free(res);
        s_fetching = false;
    }
    vTaskDelete(NULL);
}

static void start_fetch(const char *pending_status)
{
    if (s_fetching) {
        return;
    }
    if (!wifi_manager_is_connected()) {
        set_status_lbl("WiFi not connected");
        return;
    }

    s_fetching = true;
    if (pending_status) {
        set_status_lbl(pending_status);
    }
    if (s_refresh_btn) {
        lv_obj_add_state(s_refresh_btn, LV_STATE_DISABLED);
    }

    BaseType_t ok = xTaskCreate(fetch_task, "wx_fetch", 8192, NULL, 5, NULL);
    if (ok != pdPASS) {
        s_fetching = false;
        set_status_lbl("Weather task failed");
        if (s_refresh_btn) {
            lv_obj_clear_state(s_refresh_btn, LV_STATE_DISABLED);
        }
    }
}

static void refresh_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    start_fetch("Loading weather...");
}

static void period_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (wifi_manager_is_connected()) {
        start_fetch(NULL);
    }
}

void ui_weather_request_refresh(void)
{
    start_fetch(s_have_data ? NULL : "Loading weather...");
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

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0e0e18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 12, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    s_place = lv_label_create(hdr);
    lv_label_set_text(s_place, "Tuen Mun");
    lv_obj_set_style_text_font(s_place, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_place, lv_color_hex(0x93c5fd), 0);
    lv_obj_align(s_place, LV_ALIGN_LEFT_MID, 4, 0);

    s_refresh_btn = lv_button_create(hdr);
    lv_obj_set_size(s_refresh_btn, 96, 34);
    lv_obj_align(s_refresh_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(s_refresh_btn, lv_color_hex(0x2563eb), 0);
    lv_obj_t *rb_lbl = lv_label_create(s_refresh_btn);
    lv_label_set_text(rb_lbl, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_font(rb_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(rb_lbl);
    lv_obj_add_event_cb(s_refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Temp row: value + fluent C icon */
    lv_obj_t *temp_row = lv_obj_create(parent);
    lv_obj_set_width(temp_row, LV_PCT(100));
    lv_obj_set_height(temp_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(temp_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(temp_row, 0, 0);
    lv_obj_set_style_pad_all(temp_row, 0, 0);
    lv_obj_set_flex_flow(temp_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(temp_row, 6, 0);
    lv_obj_clear_flag(temp_row, LV_OBJ_FLAG_SCROLLABLE);

    s_temp = lv_label_create(temp_row);
    lv_label_set_text(s_temp, "--");
    lv_obj_set_style_text_font(s_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_temp, lv_color_hex(0xf0f4ff), 0);

    s_temp_icon = lv_image_create(temp_row);
    lv_image_set_src(s_temp_icon, &icon_temp_c);
    lv_obj_set_size(s_temp_icon, 28, 28);
    lv_obj_set_style_image_recolor(s_temp_icon, lv_color_hex(0xf0f4ff), 0);
    lv_obj_set_style_image_recolor_opa(s_temp_icon, LV_OPA_COVER, 0);

    lv_obj_t *cond_row = lv_obj_create(parent);
    lv_obj_set_width(cond_row, LV_PCT(100));
    lv_obj_set_height(cond_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cond_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cond_row, 0, 0);
    lv_obj_set_style_pad_all(cond_row, 0, 0);
    lv_obj_set_flex_flow(cond_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cond_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cond_row, 12, 0);
    lv_obj_clear_flag(cond_row, LV_OBJ_FLAG_SCROLLABLE);

    s_icon = lv_image_create(cond_row);
    lv_image_set_src(s_icon, &wx_icon_generic);
    lv_obj_set_style_image_recolor(s_icon, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);
    lv_obj_set_size(s_icon, 32, 32);

    s_condition = lv_label_create(cond_row);
    lv_label_set_text(s_condition, "Tap Refresh");
    lv_obj_set_style_text_font(s_condition, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_condition, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_flex_grow(s_condition, 1);
    lv_label_set_long_mode(s_condition, LV_LABEL_LONG_SCROLL_CIRCULAR);

    s_humidity = lv_label_create(parent);
    lv_label_set_text(s_humidity, "Humidity --");
    lv_obj_set_style_text_font(s_humidity, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_humidity, lv_color_hex(0x9aa4b8), 0);

    s_sunrise = lv_label_create(parent);
    lv_label_set_text(s_sunrise, "Sunrise --:--");
    lv_obj_set_style_text_font(s_sunrise, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sunrise, lv_color_hex(0xfbbf24), 0);

    s_sunset = lv_label_create(parent);
    lv_label_set_text(s_sunset, "Sunset --:--");
    lv_obj_set_style_text_font(s_sunset, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sunset, lv_color_hex(0xfb923c), 0);

    s_updated = lv_label_create(parent);
    lv_label_set_text(s_updated, "Updated --:--");
    lv_obj_set_style_text_font(s_updated, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_updated, lv_color_hex(0x6b7280), 0);

    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(parent);
    lv_label_set_text(s_status, "Starting...");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x6b7280), 0);
    lv_obj_set_width(s_status, LV_PCT(95));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_timer_create(period_timer_cb, WEATHER_PERIOD_MS, NULL);

    ESP_LOGI(TAG, "weather panel ready (icons + SRS)");
}