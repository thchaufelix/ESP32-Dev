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
#include "ui_topbar.h"
#include "ui_wifi_settings.h"
#include "wifi_manager.h"

static const char *TAG = "ui_app";
static bool s_ntp_started;
static bool s_time_ready;

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
                ESP_LOGI(TAG, "NTP time ready");
            }
            ui_clock_set_status_unlocked("Asia/Shanghai (UTC+8)");
        } else {
            ui_clock_set_status_unlocked("WiFi OK - syncing time...");
        }
    } else {
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
            /* state says connected but is_connected false — brief race */
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
    (void)wifi_manager_take_status_dirty(); /* clear flag; we always refresh */
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

    lv_obj_t *scr = lv_screen_active();

    ui_clock_create(scr);
    ui_topbar_create(scr, on_wifi_icon_tap, NULL);
    ui_wifi_settings_prepare(scr, on_wifi_settings_back, NULL);

    refresh_status_ui();

    /* Single poll: WiFi state, NTP, topbar icon */
    lv_timer_create(ui_poll_timer_cb, 500, NULL);

    ESP_LOGI(TAG, "UI ready (status polled every 500ms)");
    return ESP_OK;
}
