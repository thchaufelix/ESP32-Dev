#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"

#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "waveshare_rgb_lcd_port.h"
#include "wifi_config.h"

static const char *TAG = "clock";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     10

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

static lv_obj_t *s_time_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_status_label;
static bool s_time_ready = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "retry WiFi connect (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to WiFi SSID:%s", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi connect failed");
    }
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synced");
}

static void init_sntp(void)
{
    ESP_LOGI(TAG, "init SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

static void wait_for_time(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 20;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "waiting for NTP... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGE(TAG, "NTP sync failed");
        s_time_ready = false;
    } else {
        ESP_LOGI(TAG, "time ready");
        s_time_ready = true;
    }
}

static void ui_set_status(const char *text)
{
    if (s_status_label == NULL) {
        return;
    }
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        lv_label_set_text(s_status_label, text);
        esp_lv_adapter_unlock();
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    time_t now;
    struct tm timeinfo;
    char time_buf[16];
    char date_buf[32];

    time(&now);
    localtime_r(&now, &timeinfo);

    if (!s_time_ready || timeinfo.tm_year < (2020 - 1900)) {
        lv_label_set_text(s_time_label, "--:--:--");
        lv_label_set_text(s_date_label, "---- -- --");
        return;
    }

    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &timeinfo);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d  %a", &timeinfo);
    lv_label_set_text(s_time_label, time_buf);
    lv_label_set_text(s_date_label, date_buf);

    printf("CLOCK %s %s\n", date_buf, time_buf);
}

static void create_clock_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    /* screen not scrollable by default */

    s_time_label = lv_label_create(scr);
    lv_label_set_text(s_time_label, "--:--:--");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -40);

    s_date_label = lv_label_create(scr);
    lv_label_set_text(s_date_label, "---- -- --");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x9aa4b8), 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 40);

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Starting...");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x6b7280), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -24);

    /* LVGL timer runs on the adapter task; no extra lock needed inside cb */
    lv_timer_create(clock_timer_cb, 1000, NULL);
}

static void display_init(void)
{
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        tear_mode,
        rotation,
        &panel_handle,
        &touch_handle));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle,
        NULL,
        EXAMPLE_LCD_H_RES,
        EXAMPLE_LCD_V_RES,
        rotation);
    disp_config.profile.use_psram = true;

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_config);
    assert(disp != NULL);

    if (touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
        ESP_LOGI(TAG, "GT911 touch registered");
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        create_clock_ui();
        esp_lv_adapter_unlock();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 Touch LCD 7 WiFi NTP clock");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Bring up panel first so the screen is alive while WiFi/NTP run */
    display_init();
    ui_set_status("Connecting WiFi...");

    wifi_init_sta();
    ui_set_status("Syncing NTP...");

    /* Asia/Shanghai (UTC+8) */
    setenv("TZ", "CST-8", 1);
    tzset();

    init_sntp();
    wait_for_time();

    if (s_time_ready) {
        ui_set_status("Asia/Shanghai (UTC+8)");
    } else {
        ui_set_status("NTP failed — check WiFi");
    }

    /* Keep main task alive; LVGL adapter + clock timer own the UI updates */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
