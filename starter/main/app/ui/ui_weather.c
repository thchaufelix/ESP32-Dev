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
#include "fonts.h"

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
static volatile bool s_fetching;
static bool s_have_data;

static void set_status_lbl(const char *text)
{
    if (s_status && text) {
        lv_label_set_text(s_status, text);
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

static void apply_weather(const weather_data_t *w)
{
    if (w == NULL) {
        return;
    }
    s_have_data = true;

    if (s_place) {
        const char *place = w->place[0] ? w->place : "Tuen Mun";
        /* Trial TC label: map known English place to Traditional Chinese */
        if (strcmp(place, "Tuen Mun") == 0) {
            place = "屯門";
        }
        lv_label_set_text(s_place, place);
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
        const char *cond = w->condition[0] ? w->condition : "天氣";
        lv_label_set_text(s_condition, cond);
    }

    if (s_humidity) {
        if (w->humidity >= 0) {
            char buf[40];
            snprintf(buf, sizeof(buf), "%d%% (HKO)", w->humidity);
            lv_label_set_text(s_humidity, buf);
        } else {
            lv_label_set_text(s_humidity, "--");
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
            snprintf(buf, sizeof(buf), "Updated at %s", w->update_hhmm);
            lv_label_set_text(s_updated, buf);
        } else {
            lv_label_set_text(s_updated, "Updated at --:--");
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
        /* Keep last good weather on screen. */
        set_status_lbl(s_have_data ? "Refresh failed - showing last data" : "Weather fetch failed");
        ESP_LOGW(TAG, "fetch err=%s (keeping UI)", esp_err_to_name(res->err));
    } else {
        apply_weather(&res->data);
        set_status_lbl("HKO Current weather");
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

    BaseType_t ok = xTaskCreate(fetch_task, "wx_fetch", 8192, NULL, 5, NULL);
    if (ok != pdPASS) {
        s_fetching = false;
        set_status_lbl("Weather task failed");
    }
}

static void place_tap_cb(lv_event_t *e)
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
    lv_obj_set_style_pad_all(parent, 18, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 10, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* Tap place name to refresh weather */
    s_place = lv_label_create(hdr);
    lv_label_set_text(s_place, "屯門");
    lv_obj_set_style_text_font(s_place, font_tc_16(), 0);
    lv_obj_set_style_text_color(s_place, lv_color_hex(0x93c5fd), 0);
    lv_obj_align(s_place, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_flag(s_place, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_place, place_tap_cb, LV_EVENT_CLICKED, NULL);

    /*
     * Meta row SPACE_BETWEEN:
     *   left  = temperature + weather condition (same box)
     *   right = humidity / sunrise / sunset
     */
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

    /* Temperature: left-aligned inside left box */
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
    lv_label_set_text(s_temp, "--");
    lv_obj_set_style_text_font(s_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_temp, lv_color_hex(0xf0f4ff), 0);

    s_temp_icon = lv_image_create(temp_box);
    lv_image_set_src(s_temp_icon, &icon_temp_c);
    lv_obj_set_size(s_temp_icon, 40, 40);
    lv_obj_set_style_image_recolor(s_temp_icon, lv_color_hex(0xf0f4ff), 0);
    lv_obj_set_style_image_recolor_opa(s_temp_icon, LV_OPA_COVER, 0);

    /* Condition: icon + text (no Updated here) */
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
    lv_label_set_long_mode(s_condition, LV_LABEL_LONG_SCROLL_CIRCULAR);

    /* Right detail box */
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
    make_icon_text_row(detail_box, &icon_sun_up, 0xfbbf24, &s_sunrise, "Sunrise --:--");
    make_icon_text_row(detail_box, &icon_sun_down, 0xfb923c, &s_sunset, "Sunset --:--");

    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    /* Bottom: HKO Current weather + Updated at (same box, centered) */
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

    lv_timer_create(period_timer_cb, WEATHER_PERIOD_MS, NULL);

    ESP_LOGI(TAG, "weather panel ready (temp+cond; footer status+updated)");
}