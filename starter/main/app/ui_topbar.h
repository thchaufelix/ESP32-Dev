#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_topbar_wifi_tap_cb_t)(void *user);

void ui_topbar_create(lv_obj_t *parent, ui_topbar_wifi_tap_cb_t on_wifi_tap, void *user);
void ui_topbar_update(bool connected, int8_t rssi);
void ui_topbar_set_title(const char *title);
/** Enable live HH:MM once NTP has synced. */
void ui_topbar_set_time_ready(bool ready);

#ifdef __cplusplus
}
#endif