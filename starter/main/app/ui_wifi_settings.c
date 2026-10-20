#include "ui_wifi_settings.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "wifi_manager.h"

static const char *TAG = "ui_wifi";

static lv_obj_t *s_page;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_pwd_panel;
static lv_obj_t *s_ta;
static lv_obj_t *s_kb;
static lv_obj_t *s_pwd_title;

static char s_selected_ssid[WIFI_MGR_SSID_MAX + 1];
static ui_wifi_settings_back_cb_t s_back_cb;
static void *s_back_user;
static bool s_visible;

static wifi_ap_info_t s_aps[WIFI_MGR_SCAN_MAX_APS];
static size_t s_ap_count;

static void hide_password_panel(void);
static void scan_done_ui(const wifi_ap_info_t *aps, size_t count);

static void hide_password_panel(void)
{
    if (s_pwd_panel) {
        lv_obj_add_flag(s_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_kb) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, NULL);
    }
}

static void finish_and_connect(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "save & connect ssid=%s", ssid);
    lv_label_set_text(s_status, "Saving & connecting...");
    hide_password_panel();
    wifi_manager_save_and_connect(ssid, password ? password : "");
    if (s_back_cb) {
        s_back_cb(s_back_user);
    }
}

static void show_password_panel(const char *ssid, bool need_password)
{
    strncpy(s_selected_ssid, ssid, sizeof(s_selected_ssid) - 1);
    s_selected_ssid[sizeof(s_selected_ssid) - 1] = '\0';

    if (!need_password) {
        finish_and_connect(ssid, "");
        return;
    }

    char title[96];
    snprintf(title, sizeof(title), "Password: %s", ssid);
    lv_label_set_text(s_pwd_title, title);
    lv_textarea_set_text(s_ta, "");
    lv_obj_clear_flag(s_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, s_ta);
}

static void ssid_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || (size_t)idx >= s_ap_count) {
        return;
    }
    const wifi_ap_info_t *info = &s_aps[idx];
    bool need_pw = (info->authmode != WIFI_AUTH_OPEN);
    show_password_panel(info->ssid, need_pw);
}

static void populate_list(void)
{
    lv_obj_clean(s_list);

    if (s_ap_count == 0) {
        lv_list_add_button(s_list, NULL, "No networks found — tap Rescan");
        return;
    }

    for (size_t i = 0; i < s_ap_count; i++) {
        char line[96];
        const char *mark = (s_aps[i].authmode == WIFI_AUTH_OPEN) ? " " : "*";
        snprintf(line, sizeof(line), "%s %s (%d)", mark, s_aps[i].ssid, (int)s_aps[i].rssi);
        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_WIFI, line);
        lv_obj_add_event_cb(btn, ssid_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void scan_done_cb(const wifi_ap_info_t *aps, size_t count, void *user)
{
    (void)user;
    /* wifi task -> marshal into LVGL with lock */
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        return;
    }
    scan_done_ui(aps, count);
    esp_lv_adapter_unlock();
}

static void scan_done_ui(const wifi_ap_info_t *aps, size_t count)
{
    s_ap_count = (count > WIFI_MGR_SCAN_MAX_APS) ? WIFI_MGR_SCAN_MAX_APS : count;
    if (aps && s_ap_count > 0) {
        memcpy(s_aps, aps, s_ap_count * sizeof(wifi_ap_info_t));
    }
    char msg[48];
    snprintf(msg, sizeof(msg), "Found %u networks (* = locked)", (unsigned)s_ap_count);
    lv_label_set_text(s_status, msg);
    populate_list();
}

static void start_scan(void)
{
    lv_label_set_text(s_status, "Scanning 2.4GHz...");
    lv_obj_clean(s_list);
    esp_err_t err = wifi_manager_start_scan(scan_done_cb, NULL);
    if (err != ESP_OK) {
        lv_label_set_text(s_status, "Scan failed — try again");
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
    }
}

static void back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hide_password_panel();
    if (s_back_cb) {
        s_back_cb(s_back_user);
    }
}

static void rescan_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hide_password_panel();
    start_scan();
}

static void save_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    const char *pwd = lv_textarea_get_text(s_ta);
    finish_and_connect(s_selected_ssid, pwd ? pwd : "");
}

static void cancel_pwd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hide_password_panel();
}

void ui_wifi_settings_create(lv_obj_t *parent, ui_wifi_settings_back_cb_t on_back, void *user)
{
    s_back_cb = on_back;
    s_back_user = user;
    s_visible = false;

    s_page = lv_obj_create(parent);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_radius(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);

    /* Header row */
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

    lv_obj_t *rescan = lv_button_create(hdr);
    lv_obj_set_size(rescan, 100, 40);
    lv_obj_align(rescan, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_t *rescan_lbl = lv_label_create(rescan);
    lv_label_set_text(rescan_lbl, "Rescan");
    lv_obj_center(rescan_lbl);
    lv_obj_add_event_cb(rescan, rescan_btn_cb, LV_EVENT_CLICKED, NULL);

    s_status = lv_label_create(s_page);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9aa4b8), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 58);

    s_list = lv_list_create(s_page);
    lv_obj_set_size(s_list, LV_PCT(96), 280);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x151522), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);

    /* Password overlay */
    s_pwd_panel = lv_obj_create(s_page);
    lv_obj_set_size(s_pwd_panel, LV_PCT(92), 140);
    lv_obj_align(s_pwd_panel, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_color(s_pwd_panel, lv_color_hex(0x1e1e2e), 0);
    lv_obj_set_style_radius(s_pwd_panel, 8, 0);
    lv_obj_set_style_border_width(s_pwd_panel, 0, 0);
    lv_obj_add_flag(s_pwd_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pwd_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_pwd_title = lv_label_create(s_pwd_panel);
    lv_label_set_text(s_pwd_title, "Password");
    lv_obj_set_style_text_color(s_pwd_title, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(s_pwd_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ta = lv_textarea_create(s_pwd_panel);
    lv_obj_set_size(s_ta, LV_PCT(100), 40);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 28);
    lv_textarea_set_password_mode(s_ta, true);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_placeholder_text(s_ta, "Enter password");
    lv_textarea_set_max_length(s_ta, WIFI_MGR_PASS_MAX);

    lv_obj_t *save = lv_button_create(s_pwd_panel);
    lv_obj_set_size(save, 100, 40);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *save_lbl = lv_label_create(save);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save, save_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel = lv_button_create(s_pwd_panel);
    lv_obj_set_size(cancel, 100, 40);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *cancel_lbl = lv_label_create(cancel);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
    lv_obj_add_event_cb(cancel, cancel_pwd_cb, LV_EVENT_CLICKED, NULL);

    s_kb = lv_keyboard_create(s_page);
    lv_obj_set_size(s_kb, LV_PCT(100), 180);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

void ui_wifi_settings_show(void)
{
    if (s_page == NULL) {
        return;
    }
    s_visible = true;
    hide_password_panel();
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_page);
    start_scan();
}

void ui_wifi_settings_hide(void)
{
    if (s_page == NULL) {
        return;
    }
    s_visible = false;
    hide_password_panel();
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
}

bool ui_wifi_settings_is_visible(void)
{
    return s_visible;
}
