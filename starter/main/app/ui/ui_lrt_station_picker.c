#include "ui_lrt_station_picker.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_settings.h"
#include "esp_log.h"
#include "fonts.h"
#include "lrt_stations.h"

static const char *TAG = "ui_lrt_pick";

#define DISTRICT_COUNT 3

static lv_obj_t *s_parent;
static lv_obj_t *s_page;
static lv_obj_t *s_body;
static lv_obj_t *s_status;
static ui_lrt_station_picker_back_cb_t s_back_cb;
static ui_lrt_station_picker_picked_cb_t s_picked_cb;
static void *s_user;
static bool s_visible;
static bool s_built;
static int s_selected_id;
/** Collapse state for 屯門 / 元朗 / 天水圍 (index = lrt_district_t). */
static bool s_district_expanded[DISTRICT_COUNT];
static bool s_expand_inited;

static void rebuild_body(void);

static void set_status(const char *text)
{
    if (s_status && text) {
        lv_label_set_text(s_status, text);
    }
}

static void back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_back_cb) {
        s_back_cb(s_user);
    }
}

static void station_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int station_id = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "picked station_id=%d (%s)", station_id, lrt_station_name_zh(station_id));
    if (s_picked_cb) {
        s_picked_cb(s_user, station_id);
    }
}

static void pin_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    uint16_t station_id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    if (app_settings_is_lrt_pinned(station_id)) {
        esp_err_t err = app_settings_unpin_lrt_station(station_id);
        if (err != ESP_OK) {
            set_status("Unpin failed");
            ESP_LOGW(TAG, "unpin %u failed: %s", (unsigned)station_id, esp_err_to_name(err));
            return;
        }
        set_status("Unpinned");
        rebuild_body();
        return;
    }

    esp_err_t err = app_settings_pin_lrt_station(station_id);
    if (err == ESP_ERR_NO_MEM) {
        /* Block when full - do not replace oldest. */
        set_status("Max 3 pinned - unpin one first");
        return;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        set_status("Already pinned");
        return;
    }
    if (err != ESP_OK) {
        set_status("Pin failed");
        ESP_LOGW(TAG, "pin %u failed: %s", (unsigned)station_id, esp_err_to_name(err));
        return;
    }
    set_status("Pinned");
    rebuild_body();
}

static void district_hdr_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int di = (int)(intptr_t)lv_event_get_user_data(e);
    if (di < 0 || di >= DISTRICT_COUNT) {
        return;
    }
    s_district_expanded[di] = !s_district_expanded[di];
    rebuild_body();
}

static void style_station_name_btn(lv_obj_t *btn, bool selected)
{
    lv_obj_set_height(btn, 44);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn,
                              selected ? lv_color_hex(0x1e3a5f) : lv_color_hex(0x1a1a28), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, selected ? 1 : 0, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x60a5fa), 0);
    lv_obj_set_style_pad_hor(btn, 12, 0);
}

static void style_pin_btn(lv_obj_t *btn, bool pinned)
{
    lv_obj_set_size(btn, 44, 44);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn,
                              pinned ? lv_color_hex(0x3b2f1a) : lv_color_hex(0x1a1a28), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, pinned ? 1 : 0, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xfbbf24), 0);
}

/**
 * One row: [name button (flex grow)] [pin / unpin button].
 * Name picks the station; pin toggles NVS pin (block at 3).
 */
static void add_station_row(lv_obj_t *parent, int station_id, const char *name_zh,
                            bool selected, bool pinned)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_btn = lv_button_create(row);
    lv_obj_set_flex_grow(name_btn, 1);
    style_station_name_btn(name_btn, selected);
    lv_obj_add_event_cb(name_btn, station_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)station_id);

    lv_obj_t *lbl = lv_label_create(name_btn);
    lv_label_set_text(lbl, name_zh ? name_zh : "?");
    lv_obj_set_style_text_font(lbl, font_tc_20(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *pin_btn = lv_button_create(row);
    style_pin_btn(pin_btn, pinned);
    lv_obj_add_event_cb(pin_btn, pin_btn_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)(uint16_t)station_id);

    lv_obj_t *pin_lbl = lv_label_create(pin_btn);
    /* + = pin, OK = already pinned (tap to unpin). Montserrat / symbol font. */
    lv_label_set_text(pin_lbl, pinned ? LV_SYMBOL_OK : "+");
    lv_obj_set_style_text_color(pin_lbl,
                                pinned ? lv_color_hex(0xfbbf24) : lv_color_hex(0xd1d5db), 0);
    lv_obj_center(pin_lbl);
}

static void add_pinned_section(lv_obj_t *body)
{
    const app_settings_t *cfg = app_settings_get();
    int count = app_settings_lrt_pin_count();

    char title_buf[40];
    snprintf(title_buf, sizeof(title_buf), "Pinned (%d/%d)", count, APP_LRT_PIN_MAX);

    lv_obj_t *title = lv_label_create(body);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xfbbf24), 0);
    lv_obj_set_style_pad_top(title, 4, 0);
    lv_obj_set_style_pad_bottom(title, 2, 0);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(body);
        lv_label_set_text(empty, "Tap + on a station to pin (max 3)");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x6b7280), 0);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        return;
    }

    for (int i = 0; i < APP_LRT_PIN_MAX; i++) {
        uint16_t id = cfg->lrt_pinned[i];
        if (id == 0) {
            continue;
        }
        add_station_row(body, (int)id, lrt_station_name_zh((int)id),
                        (int)id == s_selected_id, true);
    }
}

static void add_district_section(lv_obj_t *body, lrt_district_t district, int selected_id)
{
    int di = (int)district;
    if (di < 0 || di >= DISTRICT_COUNT) {
        return;
    }
    bool expanded = s_district_expanded[di];

    lv_obj_t *hdr = lv_button_create(body);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_obj_set_height(hdr, 40);
    lv_obj_set_style_radius(hdr, 8, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121c), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 10, 0);
    lv_obj_add_event_cb(hdr, district_hdr_cb, LV_EVENT_CLICKED, (void *)(intptr_t)di);

    /* Chevron uses default symbol font; district name uses TC font. */
    lv_obj_t *chev = lv_label_create(hdr);
    lv_label_set_text(chev, expanded ? LV_SYMBOL_DOWN : LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chev, lv_color_hex(0x93c5fd), 0);
    lv_obj_align(chev, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, lrt_district_name_zh(district));
    lv_obj_set_style_text_font(hdr_lbl, font_tc_20(), 0);
    lv_obj_set_style_text_color(hdr_lbl, lv_color_hex(0x93c5fd), 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 22, 0);

    if (!expanded) {
        return;
    }

    size_t n = 0;
    const lrt_station_info_t *all = lrt_stations_table(&n);
    for (size_t i = 0; i < n; i++) {
        if (all[i].district != district) {
            continue;
        }
        bool selected = (all[i].id == selected_id);
        bool pinned = app_settings_is_lrt_pinned((uint16_t)all[i].id);
        add_station_row(body, all[i].id, all[i].name_zh, selected, pinned);
    }
}

static void init_expand_defaults(void)
{
    for (int i = 0; i < DISTRICT_COUNT; i++) {
        s_district_expanded[i] = false;
    }
    const lrt_station_info_t *info = lrt_station_find(s_selected_id);
    if (info) {
        s_district_expanded[(int)info->district] = true;
    } else {
        s_district_expanded[LRT_DISTRICT_TUEN_MUN] = true;
    }
    s_expand_inited = true;
}

static void rebuild_body(void)
{
    if (s_body == NULL) {
        return;
    }

    /* Keep status text across rebuild. */
    char status_keep[64] = {0};
    if (s_status) {
        const char *cur = lv_label_get_text(s_status);
        if (cur) {
            snprintf(status_keep, sizeof(status_keep), "%s", cur);
        }
    }

    lv_obj_clean(s_body);
    s_status = NULL;

    s_status = lv_label_create(s_body);
    lv_label_set_text(s_status,
                      status_keep[0] ? status_keep
                                     : "Tap station = select. + = pin (max 3). District headers collapse.");
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9aa4b8), 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);

    add_pinned_section(s_body);
    add_district_section(s_body, LRT_DISTRICT_TUEN_MUN, s_selected_id);
    add_district_section(s_body, LRT_DISTRICT_YUEN_LONG, s_selected_id);
    add_district_section(s_body, LRT_DISTRICT_TIN_SHUI_WAI, s_selected_id);
}

static void build_page(void)
{
    if (s_built || s_parent == NULL) {
        return;
    }
    s_built = true;
    ESP_LOGI(TAG, "building LRT station picker (collapse + pin)");

    s_page = lv_obj_create(s_parent);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_radius(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *hdr = lv_obj_create(s_page);
    lv_obj_set_size(hdr, LV_PCT(100), 52);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121c), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 6, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(hdr);
    lv_obj_set_size(back, 88, 40);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "時間表車站");
    lv_obj_set_style_text_font(title, font_tc_20(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    s_body = lv_obj_create(s_page);
    lv_obj_set_size(s_body, LV_PCT(100), LV_VER_RES - 52);
    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_pad_all(s_body, 12, 0);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_elastic(s_body, false);
    lv_obj_set_scroll_momentum(s_body, false);
    lv_obj_set_scrollbar_mode(s_body, LV_SCROLLBAR_MODE_AUTO);

    rebuild_body();
}

void ui_lrt_station_picker_prepare(lv_obj_t *parent,
                                   ui_lrt_station_picker_back_cb_t on_back,
                                   ui_lrt_station_picker_picked_cb_t on_picked,
                                   void *user)
{
    s_parent = parent;
    s_back_cb = on_back;
    s_picked_cb = on_picked;
    s_user = user;
    s_visible = false;
    s_built = false;
    s_page = NULL;
    s_body = NULL;
    s_status = NULL;
    s_selected_id = LRT_STATION_DEFAULT;
    s_expand_inited = false;
}

void ui_lrt_station_picker_show(void)
{
    const app_settings_t *cfg = app_settings_get();
    s_selected_id = cfg ? (int)cfg->lrt_station_id : LRT_STATION_DEFAULT;
    if (!lrt_station_id_valid(s_selected_id)) {
        s_selected_id = LRT_STATION_DEFAULT;
    }

    /* Fresh expand defaults each open (selected district expanded). */
    init_expand_defaults();

    /* Rebuild each show so highlight / pins match NVS. */
    if (s_page) {
        lv_obj_del(s_page);
        s_page = NULL;
        s_body = NULL;
        s_status = NULL;
        s_built = false;
    }
    build_page();
    if (s_page == NULL) {
        return;
    }
    s_visible = true;
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_page);
}

void ui_lrt_station_picker_hide(void)
{
    s_visible = false;
    if (s_page == NULL) {
        return;
    }
    lv_obj_del(s_page);
    s_page = NULL;
    s_body = NULL;
    s_status = NULL;
    s_built = false;
}

bool ui_lrt_station_picker_is_visible(void)
{
    return s_visible;
}
