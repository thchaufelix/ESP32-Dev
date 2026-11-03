#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_standby_prepare(lv_obj_t *parent);
void ui_standby_show(void);
void ui_standby_hide(void);
bool ui_standby_is_visible(void);

/** Pause/resume sprite-sheet animation (idle dim / wake). LVGL task. */
void ui_standby_set_anim_running(bool run);

#ifdef __cplusplus
}
#endif
