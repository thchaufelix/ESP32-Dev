#include "mio_doodle.h"

#include <stdint.h>

extern const uint8_t mio_doodle_197x240_bin_start[] asm("_binary_mio_doodle_197x240_bin_start");

#define FRAME_BYTES (MIO_DOODLE_W * MIO_DOODLE_H * 2)

const lv_image_dsc_t mio_doodle_197x240 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = MIO_DOODLE_W,
    .header.h = MIO_DOODLE_H,
    .header.stride = MIO_DOODLE_W * 2,
    .data_size = FRAME_BYTES,
    .data = mio_doodle_197x240_bin_start,
};
