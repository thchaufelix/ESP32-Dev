#include "aqua_yukata.h"

#include <stdint.h>

extern const uint8_t aqua_yukata_170x302_bin_start[] asm("_binary_aqua_yukata_170x302_bin_start");

#define FRAME_BYTES (AQUA_YUKATA_W * AQUA_YUKATA_H * 2)

const lv_image_dsc_t aqua_yukata_170x302 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = AQUA_YUKATA_W,
    .header.h = AQUA_YUKATA_H,
    .header.stride = AQUA_YUKATA_W * 2,
    .data_size = FRAME_BYTES,
    .data = aqua_yukata_170x302_bin_start,
};
