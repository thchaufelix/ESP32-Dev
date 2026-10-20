#include "ui_clock.h"

#include <stdio.h>
#include <time.h>

#include "esp_lv_adapter.h"

static lv_obj_t *s_root;
static lv_obj_t *s_time_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_status_label;
static bool s_time_ready;

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    time_t now;
    struct tm timeinfo;
    char time_buf[16];
    char date_buf[32];

    time(&now);
    localtime_r(&now, &timeinfo);

    if (!s_time_ready || timeinfo.tm_year < (2020 - 1900)) {
        lv_label_set_text(s_time_label, "--:--:--");
        lv_label_set_text(s_date_label, "---- -- --");
        return;
    }

    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &timeinfo);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d  %a", &timeinfo);
    lv_label_set_text(s_time_label, time_buf);
    lv_label_set_text(s_date_label, date_buf);
}

void ui_clock_create(lv_obj_t *parent)
{
    s_root = parent;
    s_time_ready = false;

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    s_time_label = lv_label_create(parent);
    lv_label_set_text(s_time_label, "--:--:--");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -28);

    s_date_label = lv_label_create(parent);
    lv_label_set_text(s_date_label, "---- -- --");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x9aa4b8), 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 40);

    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "Starting...");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x6b7280), 0);
    lv_obj_set_width(s_status_label, LV_PCT(90));
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_timer_create(clock_timer_cb, 1000, NULL);
}

void ui_clock_set_status_unlocked(const char *text)
{
    if (s_status_label == NULL || text == NULL) {
        return;
    }
    lv_label_set_text(s_status_label, text);
}

void ui_clock_set_status(const char *text)
{
    if (s_status_label == NULL || text == NULL) {
        return;
    }
    if (esp_lv_adapter_lock(100) == ESP_OK) {
        lv_label_set_text(s_status_label, text);
        esp_lv_adapter_unlock();
    }
}

void ui_clock_set_time_ready(bool ready)
{
    s_time_ready = ready;
}

lv_obj_t *ui_clock_get_screen(void)
{
    return s_root;
}
