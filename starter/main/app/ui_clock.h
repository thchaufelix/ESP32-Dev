#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_clock_create(lv_obj_t *parent);
void ui_clock_set_status(const char *text);
void ui_clock_set_time_ready(bool ready);
lv_obj_t *ui_clock_get_screen(void);

#ifdef __cplusplus
}
#endif
