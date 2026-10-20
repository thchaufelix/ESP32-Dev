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
static lv_obj_t *s_list;
static lv_obj_t *s_refresh_btn;
static volatile bool s_fetching;

static void set_status(const char *text)
{
    if (s_status && text) {
        lv_label_set_text(s_status, text);
    }
}

static void clear_list(void)
{
    if (s_list == NULL) {
        return;
    }
    lv_obj_clean(s_list);
}

static void add_row(const lrt_entry_t *e)
{
    char line[96];
    /* Montserrat has no CJK — show English dest/time (data still has _ch). */
    const char *dest = (e->dest_en[0] != '\0') ? e->dest_en : e->dest_ch;
    const char *tm = (e->time_en[0] != '\0') ? e->time_en : e->time_ch;
    snprintf(line, sizeof(line), "P%d  %s  %s  %s",
             e->platform_id,
             e->route_no[0] ? e->route_no : "?",
             dest[0] ? dest : "-",
             tm[0] ? tm : "-");

    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1a1a28), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_margin_bottom(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, line);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe5e7eb), 0);
}

static void apply_schedule(const lrt_schedule_t *sched)
{
    clear_list();
    if (sched == NULL || sched->count == 0) {
        set_status("No trains");
        return;
    }

    for (size_t i = 0; i < sched->count; i++) {
        add_row(&sched->entries[i]);
    }

    char st[72];
    if (sched->system_time[0] != '\0') {
        snprintf(st, sizeof(st), "Updated %s (%u)",
                 sched->system_time, (unsigned)sched->count);
    } else {
        snprintf(st, sizeof(st), "%u trains", (unsigned)sched->count);
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
        clear_list();
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

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0e0e18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Header row: title + Refresh */
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

    s_list = lv_obj_create(parent);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 8, 0);
    lv_obj_set_style_pad_all(s_list, 6, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "LRT panel ready (station %d)", LRT_STATION_LAM_TEI);
}
