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

#ifdef __cplusplus
}
#endif
