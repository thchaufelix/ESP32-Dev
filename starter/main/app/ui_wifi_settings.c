#include "ui_wifi_settings.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "wifi_manager.h"

static const char *TAG = "ui_wifi";

static lv_obj_t *s_parent;
static lv_obj_t *s_page;
static lv_obj_t *s_status;
static lv_obj_t *s_ssid_ta;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_kb;
static lv_obj_t *s_active_ta;

static ui_wifi_settings_back_cb_t s_back_cb;
static void *s_back_user;
static bool s_visible;
static bool s_built;

static void ensure_keyboard(void)
{
    if (s_kb) {
        return;
    }
    s_kb = lv_keyboard_create(s_page);
    lv_obj_set_size(s_kb, LV_PCT(100), 190);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void hide_keyboard(void)
{
    if (s_kb) {
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
    s_active_ta = NULL;
}

static void ta_focused_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED &&
        lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *ta = lv_event_get_target(e);
    ensure_keyboard();
    s_active_ta = ta;
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hide_keyboard();
    if (s_back_cb) {
        s_back_cb(s_back_user);
    }
}

static void save_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    const char *pass = lv_textarea_get_text(s_pass_ta);
    if (ssid == NULL || ssid[0] == '\0') {
        lv_label_set_text(s_status, "SSID required");
        return;
    }

    ESP_LOGI(TAG, "save & connect ssid=%s", ssid);
    lv_label_set_text(s_status, "Saving & connecting...");
    hide_keyboard();
    esp_err_t err = wifi_manager_save_and_connect(ssid, pass ? pass : "");
    if (err != ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Save/connect failed: %s", esp_err_to_name(err));
        lv_label_set_text(s_status, buf);
        return;
    }
    if (s_back_cb) {
        s_back_cb(s_back_user);
    }
}

static void build_page(void)
{
    if (s_built || s_parent == NULL) {
        return;
    }
    s_built = true;
    ESP_LOGI(TAG, "building WiFi settings page (manual SSID entry, no auto-scan)");

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
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    s_status = lv_label_create(s_page);
    lv_label_set_text(s_status, "Enter 2.4GHz SSID + password (scan disabled)");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9aa4b8), 0);
    lv_obj_set_width(s_status, LV_PCT(92));
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *ssid_lbl = lv_label_create(s_page);
    lv_label_set_text(ssid_lbl, "SSID");
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xd1d5db), 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 24, 100);

    s_ssid_ta = lv_textarea_create(s_page);
    lv_obj_set_size(s_ssid_ta, LV_PCT(90), 44);
    lv_obj_align(s_ssid_ta, LV_ALIGN_TOP_MID, 0, 124);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_textarea_set_max_length(s_ssid_ta, WIFI_MGR_SSID_MAX);
    lv_textarea_set_placeholder_text(s_ssid_ta, "Felix-Deco_IoT");
    lv_obj_add_event_cb(s_ssid_ta, ta_focused_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ssid_ta, ta_focused_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pass_lbl = lv_label_create(s_page);
    lv_label_set_text(pass_lbl, "Password");
    lv_obj_set_style_text_color(pass_lbl, lv_color_hex(0xd1d5db), 0);
    lv_obj_align(pass_lbl, LV_ALIGN_TOP_LEFT, 24, 180);

    s_pass_ta = lv_textarea_create(s_page);
    lv_obj_set_size(s_pass_ta, LV_PCT(90), 44);
    lv_obj_align(s_pass_ta, LV_ALIGN_TOP_MID, 0, 204);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_max_length(s_pass_ta, WIFI_MGR_PASS_MAX);
    lv_textarea_set_placeholder_text(s_pass_ta, "Password");
    lv_obj_add_event_cb(s_pass_ta, ta_focused_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_pass_ta, ta_focused_cb, LV_EVENT_CLICKED, NULL);

    /* Prefill from NVS / fallback if present */
    wifi_creds_t creds;
    if (wifi_manager_load_creds(&creds)) {
        lv_textarea_set_text(s_ssid_ta, creds.ssid);
        /* do not prefill password into UI for safety — user re-enters if changing */
    }

    lv_obj_t *save = lv_button_create(s_page);
    lv_obj_set_size(save, 140, 48);
    lv_obj_align(save, LV_ALIGN_TOP_MID, 0, 270);
    lv_obj_t *save_lbl = lv_label_create(save);
    lv_label_set_text(save_lbl, "Save & Connect");
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save, save_btn_cb, LV_EVENT_CLICKED, NULL);
}

void ui_wifi_settings_prepare(lv_obj_t *parent, ui_wifi_settings_back_cb_t on_back, void *user)
{
    s_parent = parent;
    s_back_cb = on_back;
    s_back_user = user;
    s_visible = false;
    s_built = false;
    s_page = NULL;
    s_kb = NULL;
}

void ui_wifi_settings_show(void)
{
    build_page();
    if (s_page == NULL) {
        return;
    }
    s_visible = true;
    hide_keyboard();
    lv_label_set_text(s_status, "Enter 2.4GHz SSID + password (scan disabled)");
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_page);
}

void ui_wifi_settings_hide(void)
{
    if (s_page == NULL) {
        s_visible = false;
        return;
    }
    s_visible = false;
    hide_keyboard();
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
}

bool ui_wifi_settings_is_visible(void)
{
    return s_visible;
}
