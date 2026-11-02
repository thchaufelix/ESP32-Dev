/* Auto-generated Ookami Mio idle RGB565 for LVGL. Do not edit. */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t still_96x144;
extern const lv_image_dsc_t idle_96x144_f0;
extern const lv_image_dsc_t idle_96x144_f1;
extern const lv_image_dsc_t idle_96x144_f2;
extern const lv_image_dsc_t idle_96x144_f3;

/** Idle loop frames f0..f3 (96x144 RGB565). */
static inline const lv_image_dsc_t *mio_idle_96x144_frame(unsigned i)
{
    static const lv_image_dsc_t *const frames[4] = {
        &idle_96x144_f0,
        &idle_96x144_f1,
        &idle_96x144_f2,
        &idle_96x144_f3,
    };
    return frames[i % 4];
}

#ifdef __cplusplus
}
#endif
