#include "fubuki_doodle.h"

#include <stdint.h>

extern const uint8_t fubuki_doodle_197x240_bin_start[] asm("_binary_fubuki_doodle_197x240_bin_start");

#define FRAME_BYTES (FUBUKI_DOODLE_W * FUBUKI_DOODLE_H * 2)

const lv_image_dsc_t fubuki_doodle_197x240 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = FUBUKI_DOODLE_W,
    .header.h = FUBUKI_DOODLE_H,
    .header.stride = FUBUKI_DOODLE_W * 2,
    .data_size = FRAME_BYTES,
    .data = fubuki_doodle_197x240_bin_start,
};
