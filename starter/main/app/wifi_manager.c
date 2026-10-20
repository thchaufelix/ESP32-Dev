#include "wifi_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

static const char *TAG = "wifi_mgr";

#define NVS_NS           "wifi_cfg"
#define NVS_KEY_SSID     "ssid"
#define NVS_KEY_PASS     "pass"
#define WIFI_MAX_RETRY   12

static EventGroupHandle_t s_event_group;
static int s_retry_num;
static wifi_mgr_state_t s_state = WIFI_MGR_STATE_IDLE;
static char s_current_ssid[WIFI_MGR_SSID_MAX + 1];

static wifi_mgr_status_cb_t s_status_cb;
static void *s_status_user;

static wifi_mgr_scan_cb_t s_scan_cb;
static void *s_scan_user;
static bool s_scan_in_progress;

static wifi_ap_info_t s_scan_aps[WIFI_MGR_SCAN_MAX_APS];

static void notify_status(wifi_mgr_state_t state)
{
    s_state = state;
    if (s_status_cb) {
        s_status_cb(state, s_status_user);
    }
}

static esp_err_t apply_sta_config(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    }
    /* Open network if empty password; else accept WPA/WPA2/WPA3-mixed */
    if (password == NULL || password[0] == '\0') {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        if (password == NULL || password[0] == '\0') {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }
    }
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    strncpy(s_current_ssid, ssid, sizeof(s_current_ssid) - 1);
    s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    return ESP_OK;
}

bool wifi_manager_load_creds(wifi_creds_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        goto fallback;
    }

    size_t ssid_len = sizeof(out->ssid);
    size_t pass_len = sizeof(out->password);
    err = nvs_get_str(h, NVS_KEY_SSID, out->ssid, &ssid_len);
    if (err == ESP_OK) {
        nvs_get_str(h, NVS_KEY_PASS, out->password, &pass_len);
    }
    nvs_close(h);

    if (err == ESP_OK && out->ssid[0] != '\0') {
        ESP_LOGI(TAG, "loaded creds from NVS ssid=%s", out->ssid);
        return true;
    }

fallback:
    if (WIFI_SSID[0] != '\0') {
        strncpy(out->ssid, WIFI_SSID, WIFI_MGR_SSID_MAX);
        strncpy(out->password, WIFI_PASSWORD, WIFI_MGR_PASS_MAX);
        ESP_LOGI(TAG, "using compile-time fallback ssid=%s", out->ssid);
        return true;
    }
    return false;
}

esp_err_t wifi_manager_save_creds(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_str(h, NVS_KEY_SSID, ssid));
    ESP_ERROR_CHECK(nvs_set_str(h, NVS_KEY_PASS, password ? password : ""));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "saved creds to NVS ssid=%s", ssid);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_current_ssid[0] != '\0' && !s_scan_in_progress) {
            notify_status(WIFI_MGR_STATE_CONNECTING);
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = event_data;
        ESP_LOGW(TAG, "disconnected reason=%d", disc ? disc->reason : -1);

        if (s_scan_in_progress) {
            return;
        }

        if (s_retry_num < WIFI_MAX_RETRY && s_current_ssid[0] != '\0') {
            s_retry_num++;
            notify_status(WIFI_MGR_STATE_CONNECTING);
            esp_wifi_connect();
            ESP_LOGW(TAG, "retry connect (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            notify_status(WIFI_MGR_STATE_FAILED);
            if (s_event_group) {
                xEventGroupSetBits(s_event_group, BIT1);
            }
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num > WIFI_MGR_SCAN_MAX_APS) {
            ap_num = WIFI_MGR_SCAN_MAX_APS;
        }

        wifi_ap_record_t records[WIFI_MGR_SCAN_MAX_APS];
        memset(records, 0, sizeof(records));
        uint16_t got = ap_num;
        esp_err_t err = esp_wifi_scan_get_ap_records(&got, records);
        size_t count = 0;
        if (err == ESP_OK) {
            for (uint16_t i = 0; i < got; i++) {
                if (records[i].ssid[0] == '\0') {
                    continue;
                }
                strncpy(s_scan_aps[count].ssid, (const char *)records[i].ssid,
                        WIFI_MGR_SSID_MAX);
                s_scan_aps[count].ssid[WIFI_MGR_SSID_MAX] = '\0';
                s_scan_aps[count].rssi = records[i].rssi;
                s_scan_aps[count].authmode = records[i].authmode;
                count++;
            }
        }

        s_scan_in_progress = false;
        wifi_mgr_scan_cb_t cb = s_scan_cb;
        void *user = s_scan_user;
        s_scan_cb = NULL;
        s_scan_user = NULL;

        /* Restore previous connecting/connected intent after scan */
        if (wifi_manager_is_connected()) {
            notify_status(WIFI_MGR_STATE_CONNECTED);
        } else if (s_current_ssid[0] != '\0') {
            notify_status(WIFI_MGR_STATE_CONNECTING);
            s_retry_num = 0;
            esp_wifi_connect();
        } else {
            notify_status(WIFI_MGR_STATE_IDLE);
        }

        if (cb) {
            cb(s_scan_aps, count, user);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        notify_status(WIFI_MGR_STATE_CONNECTED);
        if (s_event_group) {
            xEventGroupSetBits(s_event_group, BIT0);
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    s_event_group = xEventGroupCreate();
    s_retry_num = 0;
    s_state = WIFI_MGR_STATE_IDLE;
    s_current_ssid[0] = '\0';

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    wifi_creds_t creds;
    if (wifi_manager_load_creds(&creds)) {
        apply_sta_config(creds.ssid, creds.password);
        notify_status(WIFI_MGR_STATE_CONNECTING);
    } else {
        ESP_LOGW(TAG, "no saved WiFi creds; open settings to configure");
        notify_status(WIFI_MGR_STATE_IDLE);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_is_connected(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

int8_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return -127;
    }
    return ap.rssi;
}

void wifi_manager_get_ssid(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strncpy(out, (const char *)ap.ssid, out_len - 1);
        out[out_len - 1] = '\0';
    } else {
        strncpy(out, s_current_ssid, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

void wifi_manager_set_status_cb(wifi_mgr_status_cb_t cb, void *user)
{
    s_status_cb = cb;
    s_status_user = user;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (s_scan_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = apply_sta_config(ssid, password);
    if (err != ESP_OK) {
        return err;
    }
    s_retry_num = 0;
    notify_status(WIFI_MGR_STATE_CONNECTING);
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

esp_err_t wifi_manager_save_and_connect(const char *ssid, const char *password)
{
    esp_err_t err = wifi_manager_save_creds(ssid, password);
    if (err != ESP_OK) {
        return err;
    }
    return wifi_manager_connect(ssid, password);
}

esp_err_t wifi_manager_start_scan(wifi_mgr_scan_cb_t cb, void *user)
{
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_scan_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }

    s_scan_cb = cb;
    s_scan_user = user;
    s_scan_in_progress = true;
    notify_status(WIFI_MGR_STATE_SCANNING);

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 80,
        .scan_time.active.max = 120,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        s_scan_in_progress = false;
        s_scan_cb = NULL;
        s_scan_user = NULL;
        ESP_LOGE(TAG, "scan start failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "one-shot WiFi scan started");
    return ESP_OK;
}
