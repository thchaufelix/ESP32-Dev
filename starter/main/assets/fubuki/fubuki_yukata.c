#include "fubuki_yukata.h"

#include <stdint.h>

extern const uint8_t fubuki_yukata_170x302_bin_start[] asm("_binary_fubuki_yukata_170x302_bin_start");

#define FRAME_BYTES (FUBUKI_YUKATA_W * FUBUKI_YUKATA_H * 2)

const lv_image_dsc_t fubuki_yukata_170x302 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = FUBUKI_YUKATA_W,
    .header.h = FUBUKI_YUKATA_H,
    .header.stride = FUBUKI_YUKATA_W * 2,
    .data_size = FRAME_BYTES,
    .data = fubuki_yukata_170x302_bin_start,
};
