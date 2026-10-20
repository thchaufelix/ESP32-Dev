#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** GenWanMin TW Regular subset, 16px, bpp4 (Traditional Chinese UI glyphs). */
LV_FONT_DECLARE(font_genwan_tc_16);

/**
 * Wire Montserrat as fallback for Latin/digits missing from the CJK subset.
 * Call once before creating Chinese labels.
 */
void fonts_init(void);

/** Convenience: GenWan TC 16 with Montserrat fallback already set. */
static inline const lv_font_t *font_tc_16(void)
{
    return &font_genwan_tc_16;
}

#ifdef __cplusplus
}
#endif
