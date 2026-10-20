#include "ui_app.h"
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <stdio.h>

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

static void ntp_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!wifi_manager_is_connected()) {
        return;
    }
    ensure_ntp();

    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year >= (2020 - 1900)) {
        if (!s_time_ready) {
            s_time_ready = true;
            ui_clock_set_time_ready(true);
            ui_clock_set_status("Asia/Shanghai (UTC+8)");
            ESP_LOGI(TAG, "NTP time ready");
        }
    }
}

static void topbar_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    if (ui_wifi_settings_is_visible()) {
        return;
    }
    bool connected = wifi_manager_is_connected();
    int8_t rssi = connected ? wifi_manager_get_rssi() : (int8_t)-127;
    ui_topbar_update(connected, rssi);
}

static void on_wifi_status(wifi_mgr_state_t state, void *user)
{
    (void)user;
    const char *msg = "WiFi";
    switch (state) {
    case WIFI_MGR_STATE_IDLE:
        msg = "No WiFi — tap icon to setup";
        break;
    case WIFI_MGR_STATE_CONNECTING:
        msg = "Connecting WiFi...";
        break;
    case WIFI_MGR_STATE_CONNECTED:
        msg = "WiFi connected";
        ensure_ntp();
        break;
    case WIFI_MGR_STATE_FAILED:
        msg = "WiFi failed — tap icon to setup";
        break;
    case WIFI_MGR_STATE_SCANNING:
        msg = "Scanning WiFi...";
        break;
    }

    /* Status callback may run on wifi task */
    ui_clock_set_status(msg);

    if (esp_lv_adapter_lock(50) == ESP_OK) {
        bool connected = (state == WIFI_MGR_STATE_CONNECTED);
        int8_t rssi = connected ? wifi_manager_get_rssi() : (int8_t)-127;
        ui_topbar_update(connected, rssi);
        esp_lv_adapter_unlock();
    }
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
    ui_clock_set_status("Connecting WiFi...");
}

esp_err_t ui_app_init(void)
{
    s_ntp_started = false;
    s_time_ready = false;

    lv_obj_t *scr = lv_screen_active();

    ui_clock_create(scr);
    ui_topbar_create(scr, on_wifi_icon_tap, NULL);
    ui_wifi_settings_create(scr, on_wifi_settings_back, NULL);

    wifi_manager_set_status_cb(on_wifi_status, NULL);

    /* Apply current state once */
    on_wifi_status(wifi_manager_get_state(), NULL);

    lv_timer_create(topbar_refresh_cb, 2000, NULL);
    lv_timer_create(ntp_poll_timer_cb, 2000, NULL);

    ESP_LOGI(TAG, "UI ready");
    return ESP_OK;
}
