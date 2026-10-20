#include "ui_lrt_schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lrt_schedule.h"
#include "wifi_manager.h"

static const char *TAG = "ui_lrt";

static lv_obj_t *s_root;
static lv_obj_t *s_status;
static lv_obj_t *s_plat1_list;
static lv_obj_t *s_plat2_list;
static lv_obj_t *s_refresh_btn;
static volatile bool s_fetching;

static void set_status(const char *text)
{
    if (s_status && text) {
        lv_label_set_text(s_status, text);
    }
}

static void clear_lists(void)
{
    if (s_plat1_list) {
        lv_obj_clean(s_plat1_list);
    }
    if (s_plat2_list) {
        lv_obj_clean(s_plat2_list);
    }
}

/** Draw N small horizontal car icons (capped). Skip if n <= 0. */
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
        lv_obj_t *car = lv_obj_create(cars);
        lv_obj_set_size(car, 14, 9);
        lv_obj_set_style_bg_color(car, lv_color_hex(0x7dd3fc), 0);
        lv_obj_set_style_bg_opa(car, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(car, 1, 0);
        lv_obj_set_style_border_color(car, lv_color_hex(0x38bdf8), 0);
        lv_obj_set_style_radius(car, 2, 0);
        lv_obj_set_style_pad_all(car, 0, 0);
        lv_obj_clear_flag(car, LV_OBJ_FLAG_SCROLLABLE);
    }
}

/** Row: route/dest LEFT + car icons; minutes RIGHT (green). */
static void add_row(lv_obj_t *list, const lrt_entry_t *e)
{
    const char *dest = (e->dest_en[0] != '\0') ? e->dest_en : e->dest_ch;
    const char *tm = (e->time_en[0] != '\0') ? e->time_en : e->time_ch;

    char left[72];
    snprintf(left, sizeof(left), "%s  %s",
             e->route_no[0] ? e->route_no : "?",
             dest[0] ? dest : "-");

    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
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

    /* Left: route + dest + car icons */
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

    lv_obj_t *info_lbl = lv_label_create(left_wrap);
    lv_label_set_text(info_lbl, left);
    lv_label_set_long_mode(info_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(info_lbl, 1);
    lv_obj_set_style_min_width(info_lbl, 0, 0);
    lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_style_text_align(info_lbl, LV_TEXT_ALIGN_LEFT, 0);

    add_car_icons(left_wrap, e->train_length);

    /* Right: arrival minutes (green) */
    lv_obj_t *time_lbl = lv_label_create(row);
    lv_label_set_text(time_lbl, tm[0] ? tm : "-");
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(0x34d399), 0);
    lv_obj_set_style_text_align(time_lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

static lv_obj_t *make_platform_section(lv_obj_t *parent, const char *title, lv_obj_t **out_list)
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

    lv_obj_t *hdr = lv_label_create(sec);
    lv_label_set_text(hdr, title);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x93c5fd), 0);

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

    *out_list = list;
    return sec;
}

static void apply_schedule(const lrt_schedule_t *sched)
{
    clear_lists();
    if (sched == NULL || sched->count == 0) {
        set_status("No trains");
        return;
    }

    size_t n1 = 0;
    size_t n2 = 0;
    for (size_t i = 0; i < sched->count; i++) {
        const lrt_entry_t *e = &sched->entries[i];
        if (e->platform_id == 1) {
            add_row(s_plat1_list, e);
            n1++;
        } else if (e->platform_id == 2) {
            add_row(s_plat2_list, e);
            n2++;
        } else {
            /* Unexpected platform - put on P1 list */
            add_row(s_plat1_list, e);
            n1++;
        }
    }

    char st[80];
    if (sched->system_time[0] != '\0') {
        snprintf(st, sizeof(st), "Updated %s (P1:%u P2:%u)",
                 sched->system_time, (unsigned)n1, (unsigned)n2);
    } else {
        snprintf(st, sizeof(st), "P1:%u  P2:%u", (unsigned)n1, (unsigned)n2);
    }
    set_status(st);
}

typedef struct {
    esp_err_t err;
    lrt_schedule_t sched;
} fetch_result_t;

static void fetch_done_ui(void *param)
{
    fetch_result_t *res = (fetch_result_t *)param;
    s_fetching = false;
    if (s_refresh_btn) {
        lv_obj_clear_state(s_refresh_btn, LV_STATE_DISABLED);
    }

    if (res == NULL) {
        set_status("Fetch error");
        return;
    }

    if (res->err == ESP_ERR_INVALID_STATE) {
        set_status("WiFi not connected");
    } else if (res->err != ESP_OK) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Error: %s", esp_err_to_name(res->err));
        set_status(buf);
        clear_lists();
    } else {
        apply_schedule(&res->sched);
    }
    free(res);
}

static void fetch_task(void *arg)
{
    (void)arg;
    fetch_result_t *res = calloc(1, sizeof(*res));
    if (res == NULL) {
        ESP_LOGE(TAG, "result alloc failed");
        s_fetching = false;
        if (esp_lv_adapter_lock(200) == ESP_OK) {
            set_status("Out of memory");
            if (s_refresh_btn) {
                lv_obj_clear_state(s_refresh_btn, LV_STATE_DISABLED);
            }
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

static void refresh_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_fetching) {
        return;
    }
    if (!wifi_manager_is_connected()) {
        set_status("WiFi not connected");
        return;
    }

    s_fetching = true;
    set_status("Loading...");
    if (s_refresh_btn) {
        lv_obj_add_state(s_refresh_btn, LV_STATE_DISABLED);
    }

    BaseType_t ok = xTaskCreate(fetch_task, "lrt_fetch", 8192, NULL, 5, NULL);
    if (ok != pdPASS) {
        s_fetching = false;
        set_status("Task create failed");
        if (s_refresh_btn) {
            lv_obj_clear_state(s_refresh_btn, LV_STATE_DISABLED);
        }
    }
}

void ui_lrt_schedule_create(lv_obj_t *parent)
{
    s_root = parent;
    s_fetching = false;
    s_plat1_list = NULL;
    s_plat2_list = NULL;

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0e0e18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Header: title + Refresh */
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Lam Tei LRT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_refresh_btn = lv_button_create(hdr);
    lv_obj_set_size(s_refresh_btn, 96, 34);
    lv_obj_align(s_refresh_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(s_refresh_btn, lv_color_hex(0x2563eb), 0);
    lv_obj_t *rb_lbl = lv_label_create(s_refresh_btn);
    lv_label_set_text(rb_lbl, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_font(rb_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(rb_lbl);
    lv_obj_add_event_cb(s_refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    s_status = lv_label_create(parent);
    lv_label_set_text(s_status, "Tap Refresh for schedule");
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9aa4b8), 0);

    make_platform_section(parent, "Platform 1", &s_plat1_list);
    make_platform_section(parent, "Platform 2", &s_plat2_list);

    ESP_LOGI(TAG, "LRT panel ready (P1 top / P2 bottom, station %d)", LRT_STATION_LAM_TEI);
}