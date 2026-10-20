#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_wifi_settings_back_cb_t)(void *user);

/** Store parent/callbacks; does not create heavy widgets yet. */
void ui_wifi_settings_prepare(lv_obj_t *parent, ui_wifi_settings_back_cb_t on_back, void *user);

void ui_wifi_settings_show(void);
void ui_wifi_settings_hide(void);
bool ui_wifi_settings_is_visible(void);

#ifdef __cplusplus
}
#endif
