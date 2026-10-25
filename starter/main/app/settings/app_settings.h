#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MQTT_HOST_MAX 48
/** Max LRT stations pinned to the top of the station picker. */
#define APP_LRT_PIN_MAX 3

/**
 * Update schedule for LRT timetable polling.
 *
 * interval_min  - poll cadence: auto-fetch every N minutes while in period.
 * period_start_h / period_end_h - daily active window in local time (Asia/Shanghai).
 *   Hours are 0..24 (24 = midnight end-of-day).
 *   If start == end, window is always active.
 *   If start < end, active when start <= hour < end.
 *   If start > end, wraps midnight (e.g. 22..6).
 *
 * Weather is pushed by Home Assistant over MQTT (retain / HA automation); no
 * local poll interval or period window is stored for weather.
 */
typedef struct {
    uint16_t interval_min;
    uint8_t period_start_h;
    uint8_t period_end_h;
} app_update_schedule_t;

typedef struct {
    app_update_schedule_t lrt;
    /** LRT Next Train Stop ID (see lrt_stations.h). Default 藍地 = 350. */
    uint16_t lrt_station_id;
    /**
     * Pinned station IDs for the picker (top of list). Packed to the front;
     * 0 = empty slot. Max APP_LRT_PIN_MAX. Persisted in NVS.
     */
    uint16_t lrt_pinned[APP_LRT_PIN_MAX];
    /** Mosquitto / HA MQTT broker (LAN or Tailscale). Empty until set in Settings. */
    char mqtt_host[APP_MQTT_HOST_MAX];
    uint16_t mqtt_port; /* 0 => treat as 1883 */
    /**
     * Minutes with no touch before dimming backlight, stopping RGB/PCLK, and pausing LRT API
     * fetches. 0 = never auto-dim. Default 10. Clamped 0..180.
     */
    uint16_t display_idle_min;
    /**
     * Minutes with no touch before deep idle (WiFi/MQTT off + light-sleep poll).
     * 0 = disabled. When non-zero must be > display_idle_min (and idle must be > 0).
     * Measured from last activity (same clock as idle). Default 30.
     */
    uint16_t display_deep_min;
} app_settings_t;

/** Defaults: LRT 1 min / 0-24 / 藍地; no pins; mqtt host empty, port 1883; idle 10; deep 30. */
void app_settings_get_defaults(app_settings_t *out);

/** Load from NVS (or defaults). Call once at boot before UI timers. */
esp_err_t app_settings_init(void);

/** Current settings (always valid after init). */
const app_settings_t *app_settings_get(void);

/** Validate, clamp, persist to NVS, update in-memory copy. */
esp_err_t app_settings_save(const app_settings_t *in);

/**
 * Pin a station (append). Persists immediately.
 * Returns ESP_OK, ESP_ERR_INVALID_ARG (unknown id), ESP_ERR_INVALID_STATE
 * (already pinned), or ESP_ERR_NO_MEM when already APP_LRT_PIN_MAX pins
 * (does not replace - caller should show status and ask user to unpin).
 */
esp_err_t app_settings_pin_lrt_station(uint16_t station_id);

/** Unpin a station if present. Persists immediately. OK if not pinned. */
esp_err_t app_settings_unpin_lrt_station(uint16_t station_id);

/** True if station_id is currently pinned. */
bool app_settings_is_lrt_pinned(uint16_t station_id);

/** Number of non-empty pin slots (0..APP_LRT_PIN_MAX). */
int app_settings_lrt_pin_count(void);

/** True if local wall-clock hour is inside the schedule period window. */
bool app_settings_in_period(const app_update_schedule_t *sched);

/** Interval as milliseconds for lv_timer (clamped). */
uint32_t app_settings_interval_ms(const app_update_schedule_t *sched);

/** Display idle timeout as milliseconds (0 if disabled). */
uint32_t app_settings_display_idle_ms(void);

/** Deep-idle timeout as milliseconds (0 if disabled). Absolute from last activity. */
uint32_t app_settings_display_deep_ms(void);

#ifdef __cplusplus
}
#endif
