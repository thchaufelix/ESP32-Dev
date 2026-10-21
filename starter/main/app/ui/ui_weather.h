#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create right-panel weather UI. Call on LVGL task. */
void ui_weather_create(lv_obj_t *parent);

/** Thread-safe: takes LVGL adapter lock. */
void ui_weather_set_status(const char *text);
/** Call only from LVGL task / while already holding adapter lock. */
void ui_weather_set_status_unlocked(const char *text);

/** Request a background fetch if WiFi is up and idle. Safe from LVGL task. */
void ui_weather_request_refresh(void);

/** Re-read app_settings and retarget the auto-refresh timer. LVGL task. */
void ui_weather_apply_settings(void);

#ifdef __cplusplus
}
#endif
