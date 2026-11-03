#include "fubuki_idle.h"

#include <stdint.h>

extern const uint8_t fubuki_idle_270x480_0_bin_start[] asm("_binary_fubuki_idle_270x480_0_bin_start");
extern const uint8_t fubuki_idle_270x480_1_bin_start[] asm("_binary_fubuki_idle_270x480_1_bin_start");
extern const uint8_t fubuki_idle_270x480_2_bin_start[] asm("_binary_fubuki_idle_270x480_2_bin_start");
extern const uint8_t fubuki_idle_270x480_3_bin_start[] asm("_binary_fubuki_idle_270x480_3_bin_start");

#define FRAME_BYTES (FUBUKI_IDLE_FRAME_W * FUBUKI_IDLE_FRAME_H * 2)

const lv_image_dsc_t fubuki_idle_270x480_0 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = FUBUKI_IDLE_FRAME_W,
    .header.h = FUBUKI_IDLE_FRAME_H,
    .header.stride = FUBUKI_IDLE_FRAME_W * 2,
    .data_size = FRAME_BYTES,
    .data = fubuki_idle_270x480_0_bin_start,
};

const lv_image_dsc_t fubuki_idle_270x480_1 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = FUBUKI_IDLE_FRAME_W,
    .header.h = FUBUKI_IDLE_FRAME_H,
    .header.stride = FUBUKI_IDLE_FRAME_W * 2,
    .data_size = FRAME_BYTES,
    .data = fubuki_idle_270x480_1_bin_start,
};

const lv_image_dsc_t fubuki_idle_270x480_2 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = FUBUKI_IDLE_FRAME_W,
    .header.h = FUBUKI_IDLE_FRAME_H,
    .header.stride = FUBUKI_IDLE_FRAME_W * 2,
    .data_size = FRAME_BYTES,
    .data = fubuki_idle_270x480_2_bin_start,
};

const lv_image_dsc_t fubuki_idle_270x480_3 = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = FUBUKI_IDLE_FRAME_W,
    .header.h = FUBUKI_IDLE_FRAME_H,
    .header.stride = FUBUKI_IDLE_FRAME_W * 2,
    .data_size = FRAME_BYTES,
    .data = fubuki_idle_270x480_3_bin_start,
};

const lv_image_dsc_t *const fubuki_idle_frames[FUBUKI_IDLE_FRAME_COUNT] = {
    &fubuki_idle_270x480_0,
    &fubuki_idle_270x480_1,
    &fubuki_idle_270x480_2,
    &fubuki_idle_270x480_3,
};
