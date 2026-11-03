#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHLOE_IDLE_FRAME_COUNT 6
#define CHLOE_IDLE_FRAME_W 270
#define CHLOE_IDLE_FRAME_H 480

extern const lv_image_dsc_t chloe_idle_270x480_0;
extern const lv_image_dsc_t chloe_idle_270x480_1;
extern const lv_image_dsc_t chloe_idle_270x480_2;
extern const lv_image_dsc_t chloe_idle_270x480_3;
extern const lv_image_dsc_t chloe_idle_270x480_4;
extern const lv_image_dsc_t chloe_idle_270x480_5;
extern const lv_image_dsc_t *const chloe_idle_frames[CHLOE_IDLE_FRAME_COUNT];

#ifdef __cplusplus
}
#endif
