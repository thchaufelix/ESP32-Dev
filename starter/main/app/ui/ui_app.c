#include "ui_app.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_clock.h"
#include "ui_lrt_schedule.h"
#include "ui_topbar.h"
#include "ui_weather.h"
#include "ui_wifi_settings.h"
#include "ui_settings.h"
#include "ui_lrt_station_picker.h"
#include "app_settings.h"
#include "lrt_stations.h"
#include "wifi_manager.h"
#include "fonts.h"
#include "https_guard.h"

static const char *TAG = "ui_app";
static bool s_ntp_started;
static bool s_time_ready;
static bool s_weather_kicked;
static bool s_lrt_kicked;

#define TOPBAR_H 40
/** Delay between first LRT and weather HTTPS so TLS/heap are not contended. */
#define FIRST_FETCH_STAGGER_MS  6000

static void weather_kick_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_weather_request_refresh();
}

static void ensure_ntp(void)
{
    if (s_ntp_started) {
        return;
    }
    s_ntp_started = true;
    setenv("TZ", "CST-8", 1);
    tzset();
    ESP_LOGI(TAG, "init SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
}

static bool check_ntp_ready(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_year >= (2020 - 1900);
}

/** Refresh status + topbar from live WiFi/NTP — call only on LVGL task. */
static void refresh_status_ui(void)
{
    wifi_mgr_state_t state = wifi_manager_get_state();
    bool connected = wifi_manager_is_connected() || (state == WIFI_MGR_STATE_CONNECTED);

    if (connected) {
        ensure_ntp();
        if (check_ntp_ready()) {
            if (!s_time_ready) {
                s_time_ready = true;
                ui_clock_set_time_ready(true);
                ui_clock_set_status_unlocked("Asia/Shanghai (UTC+8)");
                ESP_LOGI(TAG, "NTP time ready");
            }
            /* Leave status to weather panel after NTP is ready. */
        } else {
            ui_clock_set_status_unlocked("WiFi OK - syncing time...");
        }

        /* Sequential first fetch: LRT now, weather after stagger (HTTPS guard also serializes). */
        if (!s_lrt_kicked) {
            s_lrt_kicked = true;
            ui_lrt_schedule_request_refresh();
        }
        if (!s_weather_kicked) {
            s_weather_kicked = true;
            lv_timer_t *t_wx = lv_timer_create(weather_kick_timer_cb, FIRST_FETCH_STAGGER_MS, NULL);
            lv_timer_set_repeat_count(t_wx, 1);
        }
    } else {
        s_weather_kicked = false;
        s_lrt_kicked = false;
        switch (state) {
        case WIFI_MGR_STATE_IDLE:
            ui_clock_set_status_unlocked("No WiFi - tap icon to setup");
            break;
        case WIFI_MGR_STATE_CONNECTING:
        case WIFI_MGR_STATE_SCANNING:
            ui_clock_set_status_unlocked("Connecting WiFi...");
            break;
        case WIFI_MGR_STATE_FAILED:
            ui_clock_set_status_unlocked("WiFi failed - tap icon to setup");
            break;
        case WIFI_MGR_STATE_CONNECTED:
            ui_clock_set_status_unlocked("WiFi OK - syncing time...");
            break;
        }
    }

    if (!ui_wifi_settings_is_visible() && !ui_settings_is_visible() &&
        !ui_lrt_station_picker_is_visible()) {
        int8_t rssi = connected ? wifi_manager_get_rssi() : (int8_t)-127;
        ui_topbar_update(connected, rssi);
    }
}

static void ui_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bool dirty = wifi_manager_take_status_dirty();
    /* Always progress NTP / first-fetch until ready; afterwards only on WiFi dirty
     * or every ~1s so we do not restyle the topbar every tick. */
    static uint8_t s_ticks;
    s_ticks++;
    if (!dirty && s_time_ready && s_weather_kicked && s_lrt_kicked && (s_ticks % 2) != 0) {
        return;
    }
    refresh_status_ui();
}

static void on_wifi_icon_tap(void *user)
{
    (void)user;
    ui_lrt_station_picker_hide();
    ui_settings_hide();
    ui_wifi_settings_show();
}

static void on_settings_icon_tap(void *user)
{
    (void)user;
    ui_wifi_settings_hide();
    ui_lrt_station_picker_hide();
    ui_settings_show();
}

static void on_wifi_settings_back(void *user)
{
    (void)user;
    ui_wifi_settings_hide();
    refresh_status_ui();
}

static void on_settings_back(void *user)
{
    (void)user;
    ui_lrt_station_picker_hide();
    ui_settings_hide();
    refresh_status_ui();
}

static void on_settings_applied(void *user)
{
    (void)user;
    ESP_LOGI(TAG, "settings saved - apply MQTT broker + LRT timers");
    ui_weather_apply_settings();
    ui_lrt_schedule_apply_settings();
}

static void on_station_picker_back(void *user)
{
    (void)user;
    ui_lrt_station_picker_hide();
}

static void on_station_picked(void *user, int station_id)
{
    (void)user;
    if (!lrt_station_id_valid(station_id)) {
        ESP_LOGW(TAG, "ignore invalid station_id=%d", station_id);
        ui_lrt_station_picker_hide();
        return;
    }
    app_settings_t cfg = *app_settings_get();
    cfg.lrt_station_id = (uint16_t)station_id;
    esp_err_t err = app_settings_save(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save station failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LRT station -> %d (%s)", station_id, lrt_station_name_zh(station_id));
        ui_lrt_schedule_apply_settings();
        ui_settings_refresh_station_label();
    }
    ui_lrt_station_picker_hide();
}

esp_err_t ui_app_init(void)
{
    fonts_init();
    https_guard_init();
    app_settings_init();
    s_ntp_started = false;
    s_time_ready = false;
    s_weather_kicked = false;
    s_lrt_kicked = false;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* Full-width top bar (clock centered) */
    ui_topbar_create(scr, on_wifi_icon_tap, on_settings_icon_tap, NULL);
    ui_topbar_set_title("ESP32 Dashboard");

    /* Content row below top bar: left LRT | right weather */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - TOPBAR_H);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, TOPBAR_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    /* Cross-axis START (not CENTER): avoids vertical bounce if child height jitters. */
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    lv_obj_t *left = lv_obj_create(content);
    lv_obj_set_width(left, LV_PCT(50));
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    /* Thin divider on right edge of left panel */
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(0x2a2a3a), 0);

    lv_obj_t *right = lv_obj_create(content);
    lv_obj_set_width(right, LV_PCT(50));
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    ui_lrt_schedule_create(left);
    ui_weather_create(right);
    ui_wifi_settings_prepare(scr, on_wifi_settings_back, NULL);
    ui_settings_prepare(scr, on_settings_back, on_settings_applied, NULL);
    ui_lrt_station_picker_prepare(scr, on_station_picker_back, on_station_picked, NULL);

    refresh_status_ui();
    lv_timer_create(ui_poll_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "UI ready (split LRT | weather, clock in topbar)");
    return ESP_OK;
}