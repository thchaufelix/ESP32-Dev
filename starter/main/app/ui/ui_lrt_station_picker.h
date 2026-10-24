#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_lrt_station_picker_back_cb_t)(void *user);
/** Called on LVGL task after user picks a station (before hide). */
typedef void (*ui_lrt_station_picker_picked_cb_t)(void *user, int station_id);

void ui_lrt_station_picker_prepare(lv_obj_t *parent,
                                   ui_lrt_station_picker_back_cb_t on_back,
                                   ui_lrt_station_picker_picked_cb_t on_picked,
                                   void *user);

void ui_lrt_station_picker_show(void);
void ui_lrt_station_picker_hide(void);
bool ui_lrt_station_picker_is_visible(void);

#ifdef __cplusplus
}
#endif
