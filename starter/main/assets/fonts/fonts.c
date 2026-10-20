#include "fonts.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "fonts";
static bool s_inited;

void fonts_init(void)
{
    if (s_inited) {
        return;
    }
    /* Cast away const: LVGL stores fallback on the font object at runtime. */
    lv_font_t *tc = (lv_font_t *)&font_noto_serif_tc_16;
    tc->fallback = &lv_font_montserrat_14;
    s_inited = true;
    ESP_LOGI(TAG, "Noto Serif TC 16 ready (fallback Montserrat 14)");
}
