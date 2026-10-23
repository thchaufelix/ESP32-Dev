#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MQTT_HOST_MAX 48

/**
 * Update schedule for one data source (weather or LRT).
 *
 * interval_min  — poll cadence: auto-fetch every N minutes while in period.
 * period_start_h / period_end_h — daily active window in local time (Asia/Shanghai).
 *   Hours are 0..24 (24 = midnight end-of-day).
 *   If start == end, window is always active.
 *   If start < end, active when start <= hour < end.
 *   If start > end, wraps midnight (e.g. 22..6).
 */
typedef struct {
    uint16_t interval_min;
    uint8_t period_start_h;
    uint8_t period_end_h;
} app_update_schedule_t;

typedef struct {
    app_update_schedule_t weather;
    app_update_schedule_t lrt;
    /** Mosquitto / HA MQTT broker (LAN or Tailscale). Empty until set in Settings. */
    char mqtt_host[APP_MQTT_HOST_MAX];
    uint16_t mqtt_port; /* 0 => treat as 1883 */
} app_settings_t;

/** Defaults: weather 15 min / 0-24; LRT 1 min / 0-24; mqtt host empty, port 1883. */
void app_settings_get_defaults(app_settings_t *out);

/** Load from NVS (or defaults). Call once at boot before UI timers. */
esp_err_t app_settings_init(void);

/** Current settings (always valid after init). */
const app_settings_t *app_settings_get(void);

/** Validate, clamp, persist to NVS, update in-memory copy. */
esp_err_t app_settings_save(const app_settings_t *in);

/** True if local wall-clock hour is inside the schedule period window. */
bool app_settings_in_period(const app_update_schedule_t *sched);

/** Interval as milliseconds for lv_timer (clamped). */
uint32_t app_settings_interval_ms(const app_update_schedule_t *sched);

#ifdef __cplusplus
}
#endif
