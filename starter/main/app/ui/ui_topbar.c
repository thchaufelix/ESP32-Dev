#include "ui_topbar.h"

#include <stdio.h>
#include <time.h>

static lv_obj_t *s_bar;
static lv_obj_t *s_title;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi_btn;
static lv_obj_t *s_bars[4];
static lv_obj_t *s_wifi_x;
static ui_topbar_wifi_tap_cb_t s_tap_cb;
static void *s_tap_user;
static bool s_time_ready;

#define TOPBAR_H 40

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

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_clock == NULL) {
        return;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (!s_time_ready || timeinfo.tm_year < (2020 - 1900)) {
        lv_label_set_text(s_clock, "--:--   --/-- (---)");
        return;
    }

    static const char *const k_wdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };
    int wday = timeinfo.tm_wday;
    if (wday < 0 || wday > 6) {
        wday = 0;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d   %02d/%02d (%s)",
             timeinfo.tm_hour, timeinfo.tm_min,
             timeinfo.tm_mday, timeinfo.tm_mon + 1,
             k_wdays[wday]);
    lv_label_set_text(s_clock, buf);
}

void ui_topbar_create(lv_obj_t *parent, ui_topbar_wifi_tap_cb_t on_wifi_tap, void *user)
{
    s_tap_cb = on_wifi_tap;
    s_tap_user = user;
    s_time_ready = false;

    s_bar = lv_obj_create(parent);
    lv_obj_set_size(s_bar, LV_PCT(100), TOPBAR_H);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x12121c), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bar, 4, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(s_bar);
    lv_label_set_text(s_title, "Dashboard");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xd1d5db), 0);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 4, 0);

    /* Center clock: HH:MM DD/MM (WEEKDAY) */
    s_clock = lv_label_create(s_bar);
    lv_label_set_text(s_clock, "--:--   --/-- (---)");
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, 0);

    /* Compact hit target + signal bars */
    s_wifi_btn = lv_button_create(s_bar);
    lv_obj_set_size(s_wifi_btn, 40, 28);
    lv_obj_align(s_wifi_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_opa(s_wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(s_wifi_btn, 2, 0);
    lv_obj_add_event_cb(s_wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);

    static const int heights[4] = {4, 7, 10, 13};
    for (int i = 0; i < 4; i++) {
        s_bars[i] = lv_obj_create(s_wifi_btn);
        lv_obj_set_size(s_bars[i], 4, heights[i]);
        lv_obj_set_style_radius(s_bars[i], 1, 0);
        lv_obj_set_style_border_width(s_bars[i], 0, 0);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(0x4b5563), 0);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(s_bars[i], 0, 0);
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_bars[i], LV_ALIGN_BOTTOM_LEFT, 4 + i * 7, -2);
    }

    s_wifi_x = lv_label_create(s_wifi_btn);
    lv_label_set_text(s_wifi_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(s_wifi_x, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_text_font(s_wifi_x, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wifi_x, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_wifi_x, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(clock_timer_cb, 1000, NULL);
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

void ui_topbar_set_time_ready(bool ready)
{
    s_time_ready = ready;
}