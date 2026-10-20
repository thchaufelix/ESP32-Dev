#include "ui_topbar.h"

#include <stdio.h>

static lv_obj_t *s_bar;
static lv_obj_t *s_title;
static lv_obj_t *s_wifi_btn;
static lv_obj_t *s_bars[4];
static lv_obj_t *s_wifi_x;
static ui_topbar_wifi_tap_cb_t s_tap_cb;
static void *s_tap_user;

#define TOPBAR_H 48

static void wifi_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_tap_cb) {
        s_tap_cb(s_tap_user);
    }
}

static int rssi_to_bars(int8_t rssi)
{
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -65) {
        return 3;
    }
    if (rssi >= -75) {
        return 2;
    }
    return 1;
}

void ui_topbar_create(lv_obj_t *parent, ui_topbar_wifi_tap_cb_t on_wifi_tap, void *user)
{
    s_tap_cb = on_wifi_tap;
    s_tap_user = user;

    s_bar = lv_obj_create(parent);
    lv_obj_set_size(s_bar, LV_PCT(100), TOPBAR_H);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x12121c), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bar, 8, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(s_bar);
    lv_label_set_text(s_title, "Clock");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xd1d5db), 0);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 4, 0);

    s_wifi_btn = lv_button_create(s_bar);
    lv_obj_set_size(s_wifi_btn, 72, 36);
    lv_obj_align(s_wifi_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_opa(s_wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(s_wifi_btn, 4, 0);
    lv_obj_add_event_cb(s_wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Signal bars (rightmost = strongest tier) */
    static const int heights[4] = {8, 12, 16, 20};
    for (int i = 0; i < 4; i++) {
        s_bars[i] = lv_obj_create(s_wifi_btn);
        lv_obj_set_size(s_bars[i], 8, heights[i]);
        lv_obj_set_style_radius(s_bars[i], 2, 0);
        lv_obj_set_style_border_width(s_bars[i], 0, 0);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(0x4b5563), 0);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_bars[i], LV_ALIGN_BOTTOM_LEFT, 8 + i * 12, -2);
    }

    s_wifi_x = lv_label_create(s_wifi_btn);
    lv_label_set_text(s_wifi_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(s_wifi_x, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_text_font(s_wifi_x, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wifi_x, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_wifi_x, LV_OBJ_FLAG_HIDDEN);

    ui_topbar_update(false, -127);
}

void ui_topbar_update(bool connected, int8_t rssi)
{
    if (s_wifi_btn == NULL) {
        return;
    }

    if (!connected) {
        lv_obj_clear_flag(s_wifi_x, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) {
            lv_obj_add_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_add_flag(s_wifi_x, LV_OBJ_FLAG_HIDDEN);
    int n = rssi_to_bars(rssi);
    for (int i = 0; i < 4; i++) {
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);
        lv_color_t c = (i < n) ? lv_color_hex(0x34d399) : lv_color_hex(0x374151);
        lv_obj_set_style_bg_color(s_bars[i], c, 0);
    }
}

void ui_topbar_set_title(const char *title)
{
    if (s_title && title) {
        lv_label_set_text(s_title, title);
    }
}
