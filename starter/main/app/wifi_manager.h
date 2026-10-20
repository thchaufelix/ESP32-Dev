#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MGR_SSID_MAX      32
#define WIFI_MGR_PASS_MAX      64
#define WIFI_MGR_SCAN_MAX_APS  24

typedef struct {
    char ssid[WIFI_MGR_SSID_MAX + 1];
    char password[WIFI_MGR_PASS_MAX + 1];
} wifi_creds_t;

typedef struct {
    char ssid[WIFI_MGR_SSID_MAX + 1];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_info_t;

typedef enum {
    WIFI_MGR_STATE_IDLE = 0,
    WIFI_MGR_STATE_CONNECTING,
    WIFI_MGR_STATE_CONNECTED,
    WIFI_MGR_STATE_FAILED,
    WIFI_MGR_STATE_SCANNING,
} wifi_mgr_state_t;

typedef void (*wifi_mgr_status_cb_t)(wifi_mgr_state_t state, void *user);
typedef void (*wifi_mgr_scan_cb_t)(const wifi_ap_info_t *aps, size_t count, void *user);

esp_err_t wifi_manager_init(void);

wifi_mgr_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);
int8_t wifi_manager_get_rssi(void);
void wifi_manager_get_ssid(char *out, size_t out_len);

void wifi_manager_set_status_cb(wifi_mgr_status_cb_t cb, void *user);

esp_err_t wifi_manager_save_and_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/** One-shot 2.4GHz scan. Call only from settings UI, never on boot. */
esp_err_t wifi_manager_start_scan(wifi_mgr_scan_cb_t cb, void *user);

bool wifi_manager_load_creds(wifi_creds_t *out);
esp_err_t wifi_manager_save_creds(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
