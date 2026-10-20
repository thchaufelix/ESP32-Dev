#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_clock_create(lv_obj_t *parent);
/** Thread-safe: takes LVGL adapter lock. */
void ui_clock_set_status(const char *text);
/** Call only from LVGL task / while already holding adapter lock. */
void ui_clock_set_status_unlocked(const char *text);
void ui_clock_set_time_ready(bool ready);
lv_obj_t *ui_clock_get_screen(void);

#ifdef __cplusplus
}
#endif
