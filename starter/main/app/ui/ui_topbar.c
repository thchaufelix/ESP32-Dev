#include "ui_topbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "ui_icons.h"

static lv_obj_t *s_bar;
static lv_obj_t *s_sysmon;
static lv_obj_t *s_sysmon_lbl;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi_btn;
static lv_obj_t *s_settings_btn;
static lv_obj_t *s_bars[4];
static lv_obj_t *s_wifi_x;
static ui_topbar_wifi_tap_cb_t s_wifi_tap_cb;
static ui_topbar_settings_tap_cb_t s_settings_tap_cb;
static void *s_tap_user;
static bool s_time_ready;

static bool s_wifi_drawn_connected;
static int s_wifi_drawn_n;
static bool s_wifi_drawn_valid;
static int s_last_cpu = -2;
static int s_last_dram = -1;
static int s_last_psram = -1;
static char s_last_clock[32];

#define TOPBAR_H 40

static void label_set_text_if_changed(lv_obj_t *lbl, const char *text)
{
    if (lbl == NULL || text == NULL) {
        return;
    }
    const char *cur = lv_label_get_text(lbl);
    if (cur != NULL && strcmp(cur, text) == 0) {
        return;
    }
    lv_label_set_text(lbl, text);
}

static void wifi_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_wifi_tap_cb) {
        s_wifi_tap_cb(s_tap_user);
    }
}

static void settings_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_settings_tap_cb) {
        s_settings_tap_cb(s_tap_user);
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

/** DRAM = internal SRAM (MALLOC_CAP_INTERNAL|8BIT): used for DMA/TLS/WiFi. */
static int sample_dram_usage_pct(void)
{
    size_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_sz = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (total == 0) {
        return 0;
    }
    if (free_sz > total) {
        free_sz = total;
    }
    return (int)(((total - free_sz) * 100U) / total);
}

/** PSRAM = external SPIRAM usage % (framebuffers / large buffers live here). */
static int sample_psram_usage_pct(void)
{
    size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_sz = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (total == 0) {
        return 0;
    }
    if (free_sz > total) {
        free_sz = total;
    }
    return (int)(((total - free_sz) * 100U) / total);
}

/**
 * CPU% intentionally disabled.
 * uxTaskGetSystemState() suspends the scheduler (long critical section). With
 * RGB bounce-buffer mode that starves GDMA/CPU memcpy and causes ~1s rhythmic
 * vertical drift (Espressif RGB FAQ). Show "--" instead.
 */
static int sample_cpu_usage_pct(void)
{
    return -1;
}


static void refresh_sysmon(void)
{
    if (s_sysmon_lbl == NULL) {
        return;
    }
    int cpu = sample_cpu_usage_pct();
    int dram = sample_dram_usage_pct();
    int psram = sample_psram_usage_pct();
    if (cpu == s_last_cpu && dram == s_last_dram && psram == s_last_psram) {
        return;
    }
    s_last_cpu = cpu;
    s_last_dram = dram;
    s_last_psram = psram;
    /* Fixed-width fields (%3d) + fixed pixel label width (see create). */
    char buf[48];
    if (cpu < 0) {
        snprintf(buf, sizeof(buf), "CPU: --%% DRAM:%3d%% PSRAM:%3d%%", dram, psram);
    } else {
        snprintf(buf, sizeof(buf), "CPU:%3d%% DRAM:%3d%% PSRAM:%3d%%", cpu, dram, psram);
    }
    label_set_text_if_changed(s_sysmon_lbl, buf);
}

static void refresh_clock_label(void)
{
    if (s_clock == NULL) {
        return;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (!s_time_ready || timeinfo.tm_year < (2020 - 1900)) {
        label_set_text_if_changed(s_clock, "--:--   --/-- (---)");
        s_last_clock[0] = '\0';
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
    if (strcmp(s_last_clock, buf) == 0) {
        return;
    }
    strncpy(s_last_clock, buf, sizeof(s_last_clock) - 1);
    s_last_clock[sizeof(s_last_clock) - 1] = '\0';
    label_set_text_if_changed(s_clock, buf);
}

/** Slow tick: HH:MM clock + heap sysmon. Never uxTaskGetSystemState (RGB drift). */
static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_clock_label();
    refresh_sysmon();
}

void ui_topbar_create(lv_obj_t *parent,
                      ui_topbar_wifi_tap_cb_t on_wifi_tap,
                      ui_topbar_settings_tap_cb_t on_settings_tap,
                      void *user)
{
    s_wifi_tap_cb = on_wifi_tap;
    s_settings_tap_cb = on_settings_tap;
    s_tap_user = user;
    s_time_ready = false;
    s_wifi_drawn_valid = false;
    s_last_cpu = -2;
    s_last_dram = -1;
    s_last_psram = -1;
    s_last_clock[0] = '\0';

    s_bar = lv_obj_create(parent);
    lv_obj_set_size(s_bar, LV_PCT(100), TOPBAR_H);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x12121c), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bar, 4, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* Left: speed icon + CPU%/DRAM%/PSRAM% (fixed size -> no topbar reflow / shake) */
    s_sysmon = lv_obj_create(s_bar);
    lv_obj_set_size(s_sysmon, 268, TOPBAR_H - 8);
    lv_obj_set_style_bg_opa(s_sysmon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_sysmon, 0, 0);
    lv_obj_set_style_pad_all(s_sysmon, 0, 0);
    lv_obj_set_style_radius(s_sysmon, 0, 0);
    lv_obj_set_flex_flow(s_sysmon, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_sysmon, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_sysmon, 4, 0);
    lv_obj_clear_flag(s_sysmon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_sysmon, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(s_sysmon, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_align(s_sysmon, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *speed_img = lv_image_create(s_sysmon);
    lv_image_set_src(speed_img, &icon_speed);
    lv_obj_set_size(speed_img, 20, 20);
    lv_obj_set_style_image_recolor(speed_img, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_image_recolor_opa(speed_img, LV_OPA_COVER, 0);

    s_sysmon_lbl = lv_label_create(s_sysmon);
    lv_obj_set_width(s_sysmon_lbl, 240);
    lv_label_set_long_mode(s_sysmon_lbl, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_sysmon_lbl, "CPU: --% DRAM: --% PSRAM: --%");
    lv_obj_set_style_text_font(s_sysmon_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sysmon_lbl, lv_color_hex(0xd1d5db), 0);

    /* Center clock: HH:MM DD/MM (WEEKDAY) */
    s_clock = lv_label_create(s_bar);
    lv_label_set_text(s_clock, "--:--   --/-- (---)");
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, 0);

    /* Right cluster: WiFi, then settings to its RIGHT (closer to edge). */
    s_settings_btn = lv_button_create(s_bar);
    lv_obj_set_size(s_settings_btn, 36, 28);
    lv_obj_align(s_settings_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_opa(s_settings_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_settings_btn, 0, 0);
    lv_obj_set_style_pad_all(s_settings_btn, 2, 0);
    lv_obj_add_event_cb(s_settings_btn, settings_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_img = lv_image_create(s_settings_btn);
    lv_image_set_src(settings_img, &icon_settings);
    lv_obj_set_size(settings_img, 24, 24);
    lv_obj_set_style_image_recolor(settings_img, lv_color_hex(0xd1d5db), 0);
    lv_obj_set_style_image_recolor_opa(settings_img, LV_OPA_COVER, 0);
    lv_obj_center(settings_img);

    s_wifi_btn = lv_button_create(s_bar);
    lv_obj_set_size(s_wifi_btn, 40, 28);
    lv_obj_align(s_wifi_btn, LV_ALIGN_RIGHT_MID, -40, 0);
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

    /* 15s: avoid 1Hz scheduler suspend / PSRAM spikes that underrun RGB bounce */
    lv_timer_create(clock_timer_cb, 15000, NULL);
    ui_topbar_update(false, -127);
    refresh_sysmon();
}

void ui_topbar_update(bool connected, int8_t rssi)
{
    if (s_wifi_btn == NULL) {
        return;
    }

    int n = connected ? rssi_to_bars(rssi) : 0;
    if (s_wifi_drawn_valid && s_wifi_drawn_connected == connected && s_wifi_drawn_n == n) {
        return;
    }
    s_wifi_drawn_valid = true;
    s_wifi_drawn_connected = connected;
    s_wifi_drawn_n = n;

    if (!connected) {
        lv_obj_clear_flag(s_wifi_x, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) {
            lv_obj_add_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_add_flag(s_wifi_x, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 4; i++) {
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);
        lv_color_t c = (i < n) ? lv_color_hex(0x34d399) : lv_color_hex(0x374151);
        lv_obj_set_style_bg_color(s_bars[i], c, 0);
    }
}

void ui_topbar_set_title(const char *title)
{
    (void)title;
    /* Title replaced by CPU/DRAM sysmon on the left. */
}

void ui_topbar_set_time_ready(bool ready)
{
    s_time_ready = ready;
}
