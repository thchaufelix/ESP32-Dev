#include "ui_lrt_schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_settings.h"
#include "lrt_schedule.h"
#include "wifi_manager.h"
#include "lrt_icons.h"
#include "fonts.h"

static const char *TAG = "ui_lrt";

#define LRT_UI_MAX_PLATFORMS 8

static lv_obj_t *s_root;
static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_obj_t *s_plat_hdr;
static lv_obj_t *s_title;
static lv_timer_t *s_period_timer;
static volatile bool s_fetching;
static bool s_have_data;

/** Cached last good schedule (PSRAM) so platform cycle can rebuild without refetch. */
static lrt_schedule_t *s_cached;

/** Distinct platform_id values present in s_cached, ascending. */
static int s_plat_ids[LRT_UI_MAX_PLATFORMS];
static size_t s_plat_count;
static size_t s_plat_idx;
static int s_keep_plat_id;

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

static void set_status(const char *text)
{
    if (s_status && text) {
        label_set_text_if_changed(s_status, text);
    }
}

/** Cheap fingerprint so we can skip list rebuild without caching the whole
 *  schedule in internal BSS (~3KB+). */
static uint32_t schedule_fingerprint(const lrt_schedule_t *sched)
{
    uint32_t h = 2166136261u;
    if (sched == NULL) {
        return 0;
    }
    h ^= (uint32_t)sched->count;
    h *= 16777619u;
    /* Intentionally omit system_time: clock tick must not force list rebuild (vertical shake). */
    for (size_t i = 0; i < sched->count; i++) {
        const lrt_entry_t *e = &sched->entries[i];
        h ^= (uint32_t)e->platform_id;
        h *= 16777619u;
        h ^= (uint32_t)e->train_length;
        h *= 16777619u;
        for (const char *p = e->route_no; *p; p++) { h ^= (uint8_t)(*p); h *= 16777619u; }
        for (const char *p = e->dest_ch; *p; p++) { h ^= (uint8_t)(*p); h *= 16777619u; }
        for (const char *p = e->time_en; *p; p++) { h ^= (uint8_t)(*p); h *= 16777619u; }
        for (const char *p = e->time_ch; *p; p++) { h ^= (uint8_t)(*p); h *= 16777619u; }
    }
    return h;
}

static uint32_t s_last_fp;
static bool s_have_last_fp;

/** From "YYYY-MM-DD HH:MM:SS" (or ISO T) -> "HH:MM". */
static void extract_hhmm(char *dst, size_t dst_len, const char *src)
{
    if (dst_len < 6) {
        if (dst_len) {
            dst[0] = '\0';
        }
        return;
    }
    dst[0] = '\0';
    if (src == NULL || src[0] == '\0') {
        return;
    }
    const char *p = strchr(src, ' ');
    if (p == NULL) {
        p = strchr(src, 'T');
    }
    if (p != NULL && strlen(p) >= 6) {
        p++;
        if (p[0] >= '0' && p[0] <= '9' && p[2] == ':') {
            snprintf(dst, dst_len, "%.5s", p);
            return;
        }
    }
    if (src[0] >= '0' && src[0] <= '9' && src[2] == ':') {
        snprintf(dst, dst_len, "%.5s", src);
    }
}

static void clear_list(void)
{
    if (s_list) {
        lv_obj_clean(s_list);
    }
}

static void update_plat_hdr(void)
{
    char buf[40];
    if (s_plat_count == 0) {
        snprintf(buf, sizeof(buf), "Platform -");
    } else if (s_plat_count > 1) {
        /* > hints the header is tappable / cycles (ASCII for font safety) */
        snprintf(buf, sizeof(buf), "Platform %d  >", s_plat_ids[s_plat_idx]);
    } else {
        snprintf(buf, sizeof(buf), "Platform %d", s_plat_ids[s_plat_idx]);
    }
    if (s_plat_hdr) {
        label_set_text_if_changed(s_plat_hdr, buf);
    }
}

/** Collect unique platform_id ascending from schedule into s_plat_ids. */
static void collect_platforms(const lrt_schedule_t *sched)
{
    s_plat_count = 0;
    if (sched == NULL) {
        return;
    }
    for (size_t i = 0; i < sched->count; i++) {
        int pid = sched->entries[i].platform_id;
        if (pid <= 0) {
            pid = 1; /* treat missing/0 as platform 1 */
        }
        bool found = false;
        for (size_t j = 0; j < s_plat_count; j++) {
            if (s_plat_ids[j] == pid) {
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }
        if (s_plat_count >= LRT_UI_MAX_PLATFORMS) {
            break;
        }
        /* insert sorted */
        size_t ins = s_plat_count;
        while (ins > 0 && s_plat_ids[ins - 1] > pid) {
            s_plat_ids[ins] = s_plat_ids[ins - 1];
            ins--;
        }
        s_plat_ids[ins] = pid;
        s_plat_count++;
    }
}

/** Keep current platform if still present after refresh; else first. */
static void pick_plat_idx_after_collect(void)
{
    if (s_plat_count == 0) {
        s_plat_idx = 0;
        return;
    }
    for (size_t i = 0; i < s_plat_count; i++) {
        if (s_plat_ids[i] == s_keep_plat_id) {
            s_plat_idx = i;
            return;
        }
    }
    s_plat_idx = 0;
}

/** Draw N mdi:train-car-passenger icons (one A8 asset, recolored). */
static void add_car_icons(lv_obj_t *parent, int n)
{
    if (n <= 0) {
        return;
    }
    if (n > 4) {
        n = 4;
    }

    lv_obj_t *cars = lv_obj_create(parent);
    lv_obj_set_height(cars, LV_SIZE_CONTENT);
    lv_obj_set_width(cars, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cars, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cars, 0, 0);
    lv_obj_set_style_pad_all(cars, 0, 0);
    lv_obj_set_style_radius(cars, 0, 0);
    lv_obj_set_flex_flow(cars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cars, 2, 0);
    lv_obj_clear_flag(cars, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < n; i++) {
        lv_obj_t *car = lv_image_create(cars);
        lv_image_set_src(car, &icon_train_car);
        lv_obj_set_size(car, 24, 24);
        lv_obj_set_style_image_recolor(car, lv_color_hex(0x7dd3fc), 0);
        lv_obj_set_style_image_recolor_opa(car, LV_OPA_COVER, 0);
    }
}

/** Row: route/dest LEFT + car icons; minutes RIGHT (green). */
static void add_row(lv_obj_t *list, const lrt_entry_t *e)
{
    /* Chinese dest (GenWan); English ETA (Montserrat); larger route no. */
    const char *dest = (e->dest_ch[0] != '\0') ? e->dest_ch : e->dest_en;
    const char *tm = (e->time_en[0] != '\0') ? e->time_en : e->time_ch;
    const char *route = e->route_no[0] ? e->route_no : "?";

    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_width(row, LV_PCT(100));
    /* Fixed row height keeps list geometry stable across rebuilds. */
    lv_obj_set_height(row, 40);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1a1a28), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_ver(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_margin_bottom(row, 3, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    /* Left: route (large) + dest + car icons */
    lv_obj_t *left_wrap = lv_obj_create(row);
    lv_obj_set_height(left_wrap, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_wrap, 0, 0);
    lv_obj_set_style_pad_all(left_wrap, 0, 0);
    lv_obj_set_style_radius(left_wrap, 0, 0);
    lv_obj_set_style_min_width(left_wrap, 0, 0);
    lv_obj_set_flex_grow(left_wrap, 1);
    lv_obj_set_flex_flow(left_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_wrap, 6, 0);
    lv_obj_clear_flag(left_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *route_lbl = lv_label_create(left_wrap);
    lv_label_set_text(route_lbl, route);
    lv_obj_set_style_text_font(route_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(route_lbl, lv_color_hex(0xf0f4ff), 0);

    lv_obj_t *dest_lbl = lv_label_create(left_wrap);
    lv_label_set_text(dest_lbl, dest[0] ? dest : "-");
    lv_label_set_long_mode(dest_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(dest_lbl, 1);
    lv_obj_set_style_min_width(dest_lbl, 0, 0);
    lv_obj_set_style_text_font(dest_lbl, font_tc_20(), 0);
    lv_obj_set_style_text_color(dest_lbl, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_style_text_align(dest_lbl, LV_TEXT_ALIGN_LEFT, 0);

    add_car_icons(left_wrap, e->train_length);

    /* Right: arrival minutes (green) */
    lv_obj_t *time_lbl = lv_label_create(row);
    lv_label_set_text(time_lbl, tm[0] ? tm : "-");
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(0x34d399), 0);
    lv_obj_set_style_text_align(time_lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

static void rebuild_current_platform(void)
{
    clear_list();
    update_plat_hdr();
    if (s_cached == NULL || s_plat_count == 0 || s_list == NULL) {
        return;
    }
    int pid = s_plat_ids[s_plat_idx];
    for (size_t i = 0; i < s_cached->count; i++) {
        const lrt_entry_t *e = &s_cached->entries[i];
        int eid = e->platform_id;
        if (eid <= 0) {
            eid = 1;
        }
        if (eid == pid) {
            add_row(s_list, e);
        }
    }
}

static lv_obj_t *make_platform_section(lv_obj_t *parent)
{
    lv_obj_t *sec = lv_obj_create(parent);
    lv_obj_set_width(sec, LV_PCT(100));
    lv_obj_set_flex_grow(sec, 1);
    lv_obj_set_style_bg_color(sec, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(sec, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sec, 0, 0);
    lv_obj_set_style_radius(sec, 8, 0);
    lv_obj_set_style_pad_all(sec, 6, 0);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 4, 0);
    lv_obj_clear_flag(sec, LV_OBJ_FLAG_SCROLLABLE);

    /* Clickable platform header - tap cycles to next platform */
    s_plat_hdr = lv_label_create(sec);
    lv_label_set_text(s_plat_hdr, "Platform -");
    lv_obj_set_style_text_font(s_plat_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_plat_hdr, lv_color_hex(0x93c5fd), 0);
    lv_obj_add_flag(s_plat_hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_ver(s_plat_hdr, 4, 0);
    lv_obj_set_style_pad_hor(s_plat_hdr, 2, 0);

    lv_obj_t *list = lv_obj_create(sec);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_elastic(list, false);
    lv_obj_set_scroll_momentum(list, false);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    s_list = list;
    return sec;
}

static bool ensure_cache(void)
{
    if (s_cached != NULL) {
        return true;
    }
    s_cached = heap_caps_calloc(1, sizeof(*s_cached), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_cached == NULL) {
        s_cached = calloc(1, sizeof(*s_cached));
    }
    return s_cached != NULL;
}

static void apply_schedule(const lrt_schedule_t *sched)
{
    if (sched == NULL) {
        clear_list();
        s_plat_count = 0;
        update_plat_hdr();
        set_status("No trains");
        s_have_last_fp = false;
        return;
    }

    /* Avoid full list tear-down/rebuild when payload is unchanged (stops flicker). */
    uint32_t fp = schedule_fingerprint(sched);
    if (s_have_last_fp && fp == s_last_fp) {
        char hhmm[8];
        extract_hhmm(hhmm, sizeof(hhmm), sched->system_time);
        char st[32];
        if (hhmm[0] != '\0') {
            snprintf(st, sizeof(st), "Updated at %s", hhmm);
        } else {
            snprintf(st, sizeof(st), "Updated at --:--");
        }
        set_status(st);
        if (s_cached) {
            strncpy(s_cached->system_time, sched->system_time, sizeof(s_cached->system_time) - 1);
            s_cached->system_time[sizeof(s_cached->system_time) - 1] = '\0';
        }
        return;
    }

    if (!ensure_cache()) {
        set_status("Out of memory");
        s_have_last_fp = false;
        return;
    }

    s_keep_plat_id = (s_plat_count > 0 && s_plat_idx < s_plat_count)
                         ? s_plat_ids[s_plat_idx]
                         : 0;
    *s_cached = *sched;
    collect_platforms(s_cached);
    pick_plat_idx_after_collect();

    if (s_cached->count == 0 || s_plat_count == 0) {
        clear_list();
        update_plat_hdr();
        set_status("No trains");
        s_have_last_fp = false;
        return;
    }

    rebuild_current_platform();

    char hhmm[8];
    extract_hhmm(hhmm, sizeof(hhmm), sched->system_time);
    char st[32];
    if (hhmm[0] != '\0') {
        snprintf(st, sizeof(st), "Updated at %s", hhmm);
    } else {
        snprintf(st, sizeof(st), "Updated at --:--");
    }
    s_have_data = true;
    set_status(st);
    s_last_fp = fp;
    s_have_last_fp = true;
}

typedef struct {
    esp_err_t err;
    lrt_schedule_t sched;
} fetch_result_t;

static void fetch_done_ui(void *param)
{
    fetch_result_t *res = (fetch_result_t *)param;
    s_fetching = false;

    if (res == NULL) {
        set_status("Fetch error");
        return;
    }

    if (res->err == ESP_ERR_INVALID_STATE) {
        set_status("WiFi not connected");
    } else if (res->err != ESP_OK) {
        /* Keep last good schedule on screen; only update status. */
        set_status(s_have_data ? "Refresh failed - showing last data" : "Fetch failed");
        ESP_LOGW(TAG, "fetch err=%s (keeping UI)", esp_err_to_name(res->err));
    } else {
        apply_schedule(&res->sched);
    }
    free(res);
}

static void fetch_task(void *arg)
{
    (void)arg;
    fetch_result_t *res = heap_caps_calloc(1, sizeof(*res), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (res == NULL) {
        res = calloc(1, sizeof(*res));
    }
    if (res == NULL) {
        ESP_LOGE(TAG, "result alloc failed");
        s_fetching = false;
        if (esp_lv_adapter_lock(200) == ESP_OK) {
            set_status("Out of memory");
            esp_lv_adapter_unlock();
        }
        vTaskDelete(NULL);
        return;
    }

    res->err = lrt_schedule_fetch(LRT_STATION_LAM_TEI, &res->sched);
    ESP_LOGI(TAG, "fetch done err=%s count=%u",
             esp_err_to_name(res->err), (unsigned)res->sched.count);

    if (esp_lv_adapter_lock(500) == ESP_OK) {
        fetch_done_ui(res);
        esp_lv_adapter_unlock();
    } else {
        ESP_LOGW(TAG, "LVGL lock timeout; drop result");
        free(res);
        s_fetching = false;
    }
    vTaskDelete(NULL);
}

static void start_fetch(const char *pending_status)
{
    if (s_fetching) {
        return;
    }
    if (!wifi_manager_is_connected()) {
        set_status("WiFi not connected");
        return;
    }

    s_fetching = true;
    if (pending_status) {
        set_status(pending_status);
    }

    /* Stack in PSRAM: 8KB internal stack was starving TLS (-0x008D / NO_MEM). */
    BaseType_t ok = xTaskCreateWithCaps(fetch_task, "lrt_fetch", 8192, NULL, 5, NULL,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ok = xTaskCreate(fetch_task, "lrt_fetch", 6144, NULL, 5, NULL);
    }
    if (ok != pdPASS) {
        s_fetching = false;
        set_status("Task create failed");
    }
}

static void title_tap_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    start_fetch("Loading...");
}

static void plat_tap_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_plat_count < 2 || s_cached == NULL) {
        return;
    }
    s_plat_idx = (s_plat_idx + 1) % s_plat_count;
    rebuild_current_platform();
    ESP_LOGI(TAG, "platform cycle -> id=%d (%u/%u)",
             s_plat_ids[s_plat_idx],
             (unsigned)(s_plat_idx + 1),
             (unsigned)s_plat_count);
}

static void period_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const app_settings_t *cfg = app_settings_get();
    if (!app_settings_in_period(&cfg->lrt)) {
        return;
    }
    if (wifi_manager_is_connected()) {
        start_fetch(NULL);
    }
}

void ui_lrt_schedule_apply_settings(void)
{
    const app_settings_t *cfg = app_settings_get();
    uint32_t ms = app_settings_interval_ms(&cfg->lrt);
    bool in_period = app_settings_in_period(&cfg->lrt);
    if (s_period_timer) {
        /* set_period alone keeps last_run; reset so new interval starts at Save. */
        lv_timer_set_period(s_period_timer, ms);
        lv_timer_reset(s_period_timer);
        ESP_LOGI(TAG, "timer retarget -> %u ms (interval %u min, window %u-%u, in_period=%d)",
                 (unsigned)ms,
                 (unsigned)cfg->lrt.interval_min,
                 (unsigned)cfg->lrt.period_start_h,
                 (unsigned)cfg->lrt.period_end_h,
                 (int)in_period);
    } else {
        ESP_LOGW(TAG, "apply_settings: no timer yet");
    }
    /* Immediate feedback when Save lands inside the active window. */
    if (in_period && wifi_manager_is_connected()) {
        start_fetch(NULL);
    } else if (!in_period) {
        set_status("Outside update period");
        ESP_LOGI(TAG, "skip fetch: outside period window");
    }
}

void ui_lrt_schedule_request_refresh(void)
{
    /* Boot / one-shot: quiet if we already have data */
    start_fetch(s_have_data ? NULL : "Loading...");
}

void ui_lrt_schedule_create(lv_obj_t *parent)
{
    s_root = parent;
    s_period_timer = NULL;
    s_fetching = false;
    s_have_data = false;
    s_have_last_fp = false;
    s_list = NULL;
    s_plat_hdr = NULL;
    s_plat_count = 0;
    s_plat_idx = 0;
    s_keep_plat_id = 0;
    s_cached = NULL;

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0e0e18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Header: station left, Updated at right; tap station to refresh */
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 4, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(hdr);
    lv_label_set_text(s_title, "藍地");
    lv_obj_set_style_text_font(s_title, font_tc_16(), 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xf0f4ff), 0);
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_title, title_tap_cb, LV_EVENT_CLICKED, NULL);

    s_status = lv_label_create(hdr);
    lv_label_set_text(s_status, "Updated at --:--");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9aa4b8), 0);

    make_platform_section(parent);
    lv_obj_add_event_cb(s_plat_hdr, plat_tap_cb, LV_EVENT_CLICKED, NULL);

    const app_settings_t *cfg = app_settings_get();
    s_period_timer = lv_timer_create(period_timer_cb, app_settings_interval_ms(&cfg->lrt), NULL);

    ESP_LOGI(TAG, "LRT panel ready (station %d, interval %u min, period %u-%u, one-platform cycle)",
             LRT_STATION_LAM_TEI,
             (unsigned)cfg->lrt.interval_min,
             (unsigned)cfg->lrt.period_start_h,
             (unsigned)cfg->lrt.period_end_h);
}
