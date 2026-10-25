#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create left-panel LRT schedule UI. Call on LVGL task. */
void ui_lrt_schedule_create(lv_obj_t *parent);

/** Request a background fetch if WiFi is up and idle. Safe from LVGL task. */
void ui_lrt_schedule_request_refresh(void);

/** Re-read app_settings and retarget the auto-refresh timer. LVGL task. */
void ui_lrt_schedule_apply_settings(void);

/**
 * Pause/resume periodic LRT API fetches (display idle).
 * When paused, the period timer is paused and period_timer_cb is a no-op.
 * Does not cancel an in-flight fetch. LVGL task.
 */
void ui_lrt_schedule_set_fetch_paused(bool paused);

/** True while LRT periodic fetches are paused for idle. */
bool ui_lrt_schedule_is_fetch_paused(void);

#ifdef __cplusplus
}
#endif
