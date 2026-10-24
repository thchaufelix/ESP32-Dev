#include "ui_lrt_station_picker.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_settings.h"
#include "esp_log.h"
#include "fonts.h"
#include "lrt_stations.h"

static const char *TAG = "ui_lrt_pick";

static lv_obj_t *s_parent;
static lv_obj_t *s_page;
static ui_lrt_station_picker_back_cb_t s_back_cb;
static ui_lrt_station_picker_picked_cb_t s_picked_cb;
static void *s_user;
static bool s_visible;
static bool s_built;
static int s_selected_id;

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

static void add_district_section(lv_obj_t *body, lrt_district_t district, int selected_id)
{
    size_t n = 0;
    const lrt_station_info_t *all = lrt_stations_table(&n);

    lv_obj_t *title = lv_label_create(body);
    lv_label_set_text(title, lrt_district_name_zh(district));
    lv_obj_set_style_text_font(title, font_tc_20(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_pad_top(title, 10, 0);
    lv_obj_set_style_pad_bottom(title, 4, 0);

    for (size_t i = 0; i < n; i++) {
        if (all[i].district != district) {
            continue;
        }
        bool selected = (all[i].id == selected_id);

        lv_obj_t *btn = lv_button_create(body);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 44);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn,
                                  selected ? lv_color_hex(0x1e3a5f) : lv_color_hex(0x1a1a28), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, selected ? 1 : 0, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x60a5fa), 0);
        lv_obj_add_event_cb(btn, station_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)all[i].id);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, all[i].name_zh);
        lv_obj_set_style_text_font(lbl, font_tc_20(), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xf0f4ff), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
    }
}

static void build_page(void)
{
    if (s_built || s_parent == NULL) {
        return;
    }
    s_built = true;
    ESP_LOGI(TAG, "building LRT station picker");

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

    lv_obj_t *body = lv_obj_create(s_page);
    lv_obj_set_size(body, LV_PCT(100), LV_VER_RES - 52);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_elastic(body, false);
    lv_obj_set_scroll_momentum(body, false);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "撳站即更新時間表");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_font(hint, font_tc_16(), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9aa4b8), 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    add_district_section(body, LRT_DISTRICT_TUEN_MUN, s_selected_id);
    add_district_section(body, LRT_DISTRICT_YUEN_LONG, s_selected_id);
    add_district_section(body, LRT_DISTRICT_TIN_SHUI_WAI, s_selected_id);
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
    s_selected_id = LRT_STATION_DEFAULT;
}

void ui_lrt_station_picker_show(void)
{
    const app_settings_t *cfg = app_settings_get();
    s_selected_id = cfg ? (int)cfg->lrt_station_id : LRT_STATION_DEFAULT;
    if (!lrt_station_id_valid(s_selected_id)) {
        s_selected_id = LRT_STATION_DEFAULT;
    }

    /* Rebuild each show so highlight matches current NVS selection. */
    if (s_page) {
        lv_obj_del(s_page);
        s_page = NULL;
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
    s_built = false;
}

bool ui_lrt_station_picker_is_visible(void)
{
    return s_visible;
}
