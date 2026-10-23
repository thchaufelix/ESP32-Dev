/* Small A8 weather icons (Iconify material-symbols) */
#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t wx_icon_clear_night;
extern const lv_image_dsc_t wx_icon_cloudy;
extern const lv_image_dsc_t wx_icon_cloudy_night;
extern const lv_image_dsc_t wx_icon_cold;
extern const lv_image_dsc_t wx_icon_foggy;
extern const lv_image_dsc_t wx_icon_generic;
extern const lv_image_dsc_t wx_icon_heavy_rain;
extern const lv_image_dsc_t wx_icon_hot;
extern const lv_image_dsc_t wx_icon_overcast;
extern const lv_image_dsc_t wx_icon_partly_cloudy;
extern const lv_image_dsc_t wx_icon_rain;
extern const lv_image_dsc_t wx_icon_sunny;
extern const lv_image_dsc_t wx_icon_thunderstorm;
extern const lv_image_dsc_t wx_icon_windy;

typedef struct {
    const lv_image_dsc_t *img;
    uint32_t recolor; /* 0xRRGGBB */
} wx_icon_style_t;

/** Map HKO rhrread icon code to image + recolor. */
wx_icon_style_t wx_icon_style_for_hko(int hko_icon);

/** Map Home Assistant weather entity state to image + recolor. */
wx_icon_style_t wx_icon_style_for_ha(const char *state);

#ifdef __cplusplus
}
#endif
