#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FUBUKI_IDLE_FRAME_COUNT 4
#define FUBUKI_IDLE_FRAME_W 270
#define FUBUKI_IDLE_FRAME_H 480

extern const lv_image_dsc_t fubuki_idle_270x480_0;
extern const lv_image_dsc_t fubuki_idle_270x480_1;
extern const lv_image_dsc_t fubuki_idle_270x480_2;
extern const lv_image_dsc_t fubuki_idle_270x480_3;
extern const lv_image_dsc_t *const fubuki_idle_frames[FUBUKI_IDLE_FRAME_COUNT];

#ifdef __cplusplus
}
#endif
