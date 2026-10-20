#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create left-panel LRT schedule UI (list + Refresh). Call on LVGL task. */
void ui_lrt_schedule_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
