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
#include "wifi_manager.h"

static const char *TAG = "ui_app";
static bool s_ntp_started;
static bool s_time_ready;
static bool s_weather_kicked;

#define TOPBAR_H 40

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

        if (!s_weather_kicked) {
            s_weather_kicked = true;
            ui_weather_request_refresh();
        }
    } else {
        s_weather_kicked = false;
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

    if (!ui_wifi_settings_is_visible()) {
        int8_t rssi = connected ? wifi_manager_get_rssi() : (int8_t)-127;
        ui_topbar_update(connected, rssi);
    }
}

static void ui_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    (void)wifi_manager_take_status_dirty();
    refresh_status_ui();
}

static void on_wifi_icon_tap(void *user)
{
    (void)user;
    ui_wifi_settings_show();
}

static void on_wifi_settings_back(void *user)
{
    (void)user;
    ui_wifi_settings_hide();
    refresh_status_ui();
}

esp_err_t ui_app_init(void)
{
    s_ntp_started = false;
    s_time_ready = false;
    s_weather_kicked = false;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Full-width top bar (clock centered) */
    ui_topbar_create(scr, on_wifi_icon_tap, NULL);
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
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(content);
    lv_obj_set_width(left, LV_PCT(50));
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
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

    ui_lrt_schedule_create(left);
    ui_weather_create(right);
    ui_wifi_settings_prepare(scr, on_wifi_settings_back, NULL);

    refresh_status_ui();
    lv_timer_create(ui_poll_timer_cb, 500, NULL);

    ESP_LOGI(TAG, "UI ready (split LRT | weather, clock in topbar)");
    return ESP_OK;
}