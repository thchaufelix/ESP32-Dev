#include "chloe_idle.h"

#include <stdint.h>

extern const uint8_t chloe_idle_270x480_0_bin_start[] asm("_binary_chloe_idle_270x480_0_bin_start");
extern const uint8_t chloe_idle_270x480_1_bin_start[] asm("_binary_chloe_idle_270x480_1_bin_start");
extern const uint8_t chloe_idle_270x480_2_bin_start[] asm("_binary_chloe_idle_270x480_2_bin_start");
extern const uint8_t chloe_idle_270x480_3_bin_start[] asm("_binary_chloe_idle_270x480_3_bin_start");
extern const uint8_t chloe_idle_270x480_4_bin_start[] asm("_binary_chloe_idle_270x480_4_bin_start");
extern const uint8_t chloe_idle_270x480_5_bin_start[] asm("_binary_chloe_idle_270x480_5_bin_start");

#define FRAME_BYTES (CHLOE_IDLE_FRAME_W * CHLOE_IDLE_FRAME_H * 2)

#define CHLOE_IDLE_DSC(sym)                               \
    {                                                     \
        .header.magic = LV_IMAGE_HEADER_MAGIC,            \
        .header.cf = LV_COLOR_FORMAT_RGB565,              \
        .header.flags = 0,                                \
        .header.w = CHLOE_IDLE_FRAME_W,                   \
        .header.h = CHLOE_IDLE_FRAME_H,                   \
        .header.stride = CHLOE_IDLE_FRAME_W * 2,          \
        .data_size = FRAME_BYTES,                         \
        .data = (sym),                                    \
    }

const lv_image_dsc_t chloe_idle_270x480_0 = CHLOE_IDLE_DSC(chloe_idle_270x480_0_bin_start);
const lv_image_dsc_t chloe_idle_270x480_1 = CHLOE_IDLE_DSC(chloe_idle_270x480_1_bin_start);
const lv_image_dsc_t chloe_idle_270x480_2 = CHLOE_IDLE_DSC(chloe_idle_270x480_2_bin_start);
const lv_image_dsc_t chloe_idle_270x480_3 = CHLOE_IDLE_DSC(chloe_idle_270x480_3_bin_start);
const lv_image_dsc_t chloe_idle_270x480_4 = CHLOE_IDLE_DSC(chloe_idle_270x480_4_bin_start);
const lv_image_dsc_t chloe_idle_270x480_5 = CHLOE_IDLE_DSC(chloe_idle_270x480_5_bin_start);

const lv_image_dsc_t *const chloe_idle_frames[CHLOE_IDLE_FRAME_COUNT] = {
    &chloe_idle_270x480_0,
    &chloe_idle_270x480_1,
    &chloe_idle_270x480_2,
    &chloe_idle_270x480_3,
    &chloe_idle_270x480_4,
    &chloe_idle_270x480_5,
};
