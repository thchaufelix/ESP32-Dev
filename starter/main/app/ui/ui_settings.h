#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_settings_back_cb_t)(void *user);
/** Called on LVGL task after Save successfully persists settings. */
typedef void (*ui_settings_applied_cb_t)(void *user);

void ui_settings_prepare(lv_obj_t *parent,
                         ui_settings_back_cb_t on_back,
                         ui_settings_applied_cb_t on_applied,
                         void *user);

void ui_settings_show(void);
void ui_settings_hide(void);
bool ui_settings_is_visible(void);

/** Refresh the LRT station row label from app_settings (LVGL task). */
void ui_settings_refresh_station_label(void);

#ifdef __cplusplus
}
#endif
