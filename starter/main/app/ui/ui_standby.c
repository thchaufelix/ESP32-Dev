#include "ui_standby.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "fubuki_idle.h"

static const char *TAG = "ui_standby";

#define STANDBY_FPS 4
#define STANDBY_FRAME_MS (1000 / STANDBY_FPS)
#define DOUBLE_TAP_MS 1000

static lv_obj_t *s_parent;
static lv_obj_t *s_page;
static lv_obj_t *s_img;
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_wday;
static lv_timer_t *s_anim_timer;
static lv_timer_t *s_clock_timer;
static uint8_t s_frame;
static uint32_t s_last_tap_ms;
static bool s_visible;
static bool s_built;
static bool s_anim_ok;
static char s_last_time[16];
static char s_last_date[16];
static char s_last_wday[8];

static void apply_anim_timer(void)
{
    if (s_anim_timer == NULL) {
        return;
    }
    if (s_visible && s_anim_ok) {
        lv_timer_resume(s_anim_timer);
    } else {
        lv_timer_pause(s_anim_timer);
    }
}

static void apply_clock_timer(void)
{
    if (s_clock_timer == NULL) {
        return;
    }
    if (s_visible) {
        lv_timer_resume(s_clock_timer);
    } else {
        lv_timer_pause(s_clock_timer);
    }
}

static void refresh_clock(void)
{
    if (s_time == NULL || s_date == NULL || s_wday == NULL) {
        return;
    }

    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);

    char tbuf[16];
    char dbuf[16];
    char wbuf[8];
    if (ti.tm_year < (2020 - 1900)) {
        snprintf(tbuf, sizeof(tbuf), "--:--");
        snprintf(dbuf, sizeof(dbuf), "--/--");
        snprintf(wbuf, sizeof(wbuf), "---");
    } else {
        static const char *const k_wdays[] = {
            "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
        };
        int wday = ti.tm_wday;
        if (wday < 0 || wday > 6) {
            wday = 0;
        }
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
        snprintf(dbuf, sizeof(dbuf), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
        snprintf(wbuf, sizeof(wbuf), "%s", k_wdays[wday]);
    }

    if (strcmp(s_last_time, tbuf) != 0) {
        snprintf(s_last_time, sizeof(s_last_time), "%s", tbuf);
        lv_label_set_text(s_time, tbuf);
    }
    if (strcmp(s_last_date, dbuf) != 0) {
        snprintf(s_last_date, sizeof(s_last_date), "%s", dbuf);
        lv_label_set_text(s_date, dbuf);
    }
    if (strcmp(s_last_wday, wbuf) != 0) {
        snprintf(s_last_wday, sizeof(s_last_wday), "%s", wbuf);
        lv_label_set_text(s_wday, wbuf);
    }
}

static void anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_visible || s_img == NULL) {
        return;
    }
    s_frame = (uint8_t)((s_frame + 1) % FUBUKI_IDLE_FRAME_COUNT);
    lv_image_set_src(s_img, fubuki_idle_frames[s_frame]);
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_visible) {
        refresh_clock();
    }
}

static void page_clicked_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    uint32_t now = lv_tick_get();
    if (s_last_tap_ms != 0 && lv_tick_elaps(s_last_tap_ms) <= DOUBLE_TAP_MS) {
        s_last_tap_ms = 0;
        ESP_LOGI(TAG, "double-tap -> home");
        ui_standby_hide();
        return;
    }
    s_last_tap_ms = now;
}

static void style_plain(lv_obj_t *obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static void build_page(void)
{
    if (s_built || s_parent == NULL) {
        return;
    }
    s_built = true;

    s_page = lv_obj_create(s_parent);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_radius(s_page, 0, 0);
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_page, page_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *left = lv_obj_create(s_page);
    lv_obj_set_width(left, LV_PCT(70));
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_bg_color(left, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    style_plain(left);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_img = lv_image_create(left);
    lv_image_set_src(s_img, fubuki_idle_frames[0]);
    lv_obj_set_size(s_img, FUBUKI_IDLE_FRAME_W, FUBUKI_IDLE_FRAME_H);
    lv_obj_clear_flag(s_img, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *right = lv_obj_create(s_page);
    lv_obj_set_width(right, LV_PCT(30));
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_style_bg_color(right, lv_color_hex(0x0e0e18), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    style_plain(right);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right, 12, 0);
    lv_obj_set_style_pad_hor(right, 8, 0);

    s_time = lv_label_create(right);
    lv_label_set_text(s_time, "--:--");
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time, lv_color_hex(0xf0f4ff), 0);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_time, LV_PCT(100));

    s_date = lv_label_create(right);
    lv_label_set_text(s_date, "--/--");
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_date, lv_color_hex(0xd1d5db), 0);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_date, LV_PCT(100));

    s_wday = lv_label_create(right);
    lv_label_set_text(s_wday, "---");
    lv_obj_set_style_text_font(s_wday, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wday, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_text_align(s_wday, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_wday, LV_PCT(100));

    s_anim_timer = lv_timer_create(anim_timer_cb, STANDBY_FRAME_MS, NULL);
    s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_pause(s_anim_timer);
    lv_timer_pause(s_clock_timer);

    ESP_LOGI(TAG, "built (%dx%d, %d fps, double-tap %d ms)",
             FUBUKI_IDLE_FRAME_W, FUBUKI_IDLE_FRAME_H, STANDBY_FPS, DOUBLE_TAP_MS);
}

void ui_standby_prepare(lv_obj_t *parent)
{
    s_parent = parent;
    s_visible = false;
    s_built = false;
    s_page = NULL;
    s_img = NULL;
    s_anim_timer = NULL;
    s_clock_timer = NULL;
    s_anim_ok = true;
}

void ui_standby_show(void)
{
    build_page();
    if (s_page == NULL) {
        return;
    }
    s_frame = 0;
    s_last_tap_ms = 0;
    s_last_time[0] = '\0';
    s_last_date[0] = '\0';
    s_last_wday[0] = '\0';
    lv_image_set_src(s_img, fubuki_idle_frames[0]);
    refresh_clock();
    s_visible = true;
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_page);
    apply_anim_timer();
    apply_clock_timer();
    ESP_LOGI(TAG, "show");
}

void ui_standby_hide(void)
{
    s_visible = false;
    s_last_tap_ms = 0;
    apply_anim_timer();
    apply_clock_timer();
    if (s_page != NULL) {
        lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_standby_is_visible(void)
{
    return s_visible;
}

void ui_standby_set_anim_running(bool run)
{
    s_anim_ok = run;
    apply_anim_timer();
}
