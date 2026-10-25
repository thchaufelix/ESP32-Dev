#include "ui_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_settings.h"
#include "esp_log.h"
#include "fonts.h"
#include "lrt_stations.h"
#include "ui_lrt_station_picker.h"

static const char *TAG = "ui_settings";

typedef struct {
    lv_obj_t *val_lbl;
    int *value;
    int lo;
    int hi;
    const char *suffix;
} spinner_ctx_t;

static lv_obj_t *s_parent;
static lv_obj_t *s_page;
static lv_obj_t *s_status;
static lv_obj_t *s_mqtt_host_ta;
static lv_obj_t *s_station_btn_lbl;
static lv_obj_t *s_kb;
static lv_obj_t *s_kb_shade;
static ui_settings_back_cb_t s_back_cb;
static ui_settings_applied_cb_t s_applied_cb;
static void *s_user;
static bool s_visible;
static bool s_built;

static spinner_ctx_t s_spinners[8];
static int s_spinner_count;

static int s_lrt_int;
static int s_lrt_ps;
static int s_lrt_pe;
static int s_mqtt_port;
static int s_idle_min;

static void format_val(char *buf, size_t n, int v, const char *suffix)
{
    if (suffix && suffix[0]) {
        snprintf(buf, n, "%d %s", v, suffix);
    } else {
        snprintf(buf, n, "%d", v);
    }
}

static void refresh_spinner(spinner_ctx_t *ctx)
{
    if (ctx == NULL || ctx->val_lbl == NULL || ctx->value == NULL) {
        return;
    }
    char buf[24];
    format_val(buf, sizeof(buf), *ctx->value, ctx->suffix);
    lv_label_set_text(ctx->val_lbl, buf);
}

static void spinner_adjust(spinner_ctx_t *ctx, int delta)
{
    if (ctx == NULL || ctx->value == NULL) {
        return;
    }
    int v = *ctx->value + delta;
    if (v < ctx->lo) {
        v = ctx->lo;
    }
    if (v > ctx->hi) {
        v = ctx->hi;
    }
    *ctx->value = v;
    refresh_spinner(ctx);
}

static void spinner_minus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    spinner_adjust((spinner_ctx_t *)lv_event_get_user_data(e), -1);
}

static void spinner_plus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    spinner_adjust((spinner_ctx_t *)lv_event_get_user_data(e), 1);
}

static lv_obj_t *add_spinner_row(lv_obj_t *parent, const char *label, int *value,
                                 int lo, int hi, const char *suffix)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xd1d5db), 0);

    lv_obj_t *ctrl = lv_obj_create(row);
    lv_obj_set_size(ctrl, 168, 40);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl, 0, 0);
    lv_obj_set_style_pad_all(ctrl, 0, 0);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl, 6, 0);
    lv_obj_clear_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);

    spinner_ctx_t *ctx = &s_spinners[s_spinner_count++];
    ctx->value = value;
    ctx->lo = lo;
    ctx->hi = hi;
    ctx->suffix = suffix;

    lv_obj_t *minus = lv_button_create(ctrl);
    lv_obj_set_size(minus, 40, 36);
    lv_obj_t *minus_lbl = lv_label_create(minus);
    lv_label_set_text(minus_lbl, "-");
    lv_obj_center(minus_lbl);
    lv_obj_add_event_cb(minus, spinner_minus_cb, LV_EVENT_CLICKED, ctx);

    ctx->val_lbl = lv_label_create(ctrl);
    lv_obj_set_width(ctx->val_lbl, 72);
    lv_obj_set_style_text_align(ctx->val_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ctx->val_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->val_lbl, lv_color_hex(0xf0f4ff), 0);
    refresh_spinner(ctx);

    lv_obj_t *plus = lv_button_create(ctrl);
    lv_obj_set_size(plus, 40, 36);
    lv_obj_t *plus_lbl = lv_label_create(plus);
    lv_label_set_text(plus_lbl, "+");
    lv_obj_center(plus_lbl);
    lv_obj_add_event_cb(plus, spinner_plus_cb, LV_EVENT_CLICKED, ctx);

    return row;
}

static lv_obj_t *add_section_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_pad_top(lbl, 8, 0);
    lv_obj_set_style_pad_bottom(lbl, 2, 0);
    return lbl;
}

static void hide_keyboard(void)
{
    if (s_kb) {
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_kb_shade) {
        lv_obj_add_flag(s_kb_shade, LV_OBJ_FLAG_HIDDEN);
    }
}

static void kb_shade_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        hide_keyboard();
    }
}

static void ensure_keyboard(void)
{
    if (s_kb || s_page == NULL) {
        return;
    }
    s_kb_shade = lv_obj_create(s_page);
    lv_obj_set_size(s_kb_shade, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_kb_shade, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_kb_shade, 0, 0);
    lv_obj_add_flag(s_kb_shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_kb_shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb_shade, kb_shade_cb, LV_EVENT_CLICKED, NULL);

    s_kb = lv_keyboard_create(s_page);
    /* TEXT mode so hostname / Tailscale MagicDNS also works */
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_size(s_kb, LV_PCT(100), 190);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void ta_focused_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED &&
        lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *ta = lv_event_get_target(e);
    ensure_keyboard();
    if (s_kb_shade) {
        lv_obj_clear_flag(s_kb_shade, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb_shade);
    }
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_kb);
}

static void refresh_station_label(void)
{
    if (s_station_btn_lbl == NULL) {
        return;
    }
    const app_settings_t *cur = app_settings_get();
    int sid = cur ? (int)cur->lrt_station_id : LRT_STATION_DEFAULT;
    lv_label_set_text(s_station_btn_lbl, lrt_station_name_zh(sid));
}

void ui_settings_refresh_station_label(void)
{
    refresh_station_label();
}

static void station_pick_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hide_keyboard();
    ui_lrt_station_picker_show();
}

static void back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hide_keyboard();
    if (s_back_cb) {
        s_back_cb(s_user);
    }
}

static void save_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hide_keyboard();

    /* Start from current settings so pins (and other fields) are preserved. */
    app_settings_t out = *app_settings_get();
    out.lrt.interval_min = (uint16_t)s_lrt_int;
    out.lrt.period_start_h = (uint8_t)s_lrt_ps;
    out.lrt.period_end_h = (uint8_t)s_lrt_pe;
    out.mqtt_port = (uint16_t)s_mqtt_port;
    out.display_idle_min = (uint16_t)s_idle_min;
    const char *host = s_mqtt_host_ta ? lv_textarea_get_text(s_mqtt_host_ta) : "";
    snprintf(out.mqtt_host, sizeof(out.mqtt_host), "%s", host ? host : "");

    esp_err_t err = app_settings_save(&out);
    if (err != ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Save failed: %s", esp_err_to_name(err));
        lv_label_set_text(s_status, buf);
        return;
    }

    lv_label_set_text(s_status, "Saved");
    if (s_applied_cb) {
        s_applied_cb(s_user);
    }
    if (s_back_cb) {
        s_back_cb(s_user);
    }
}

static void load_draft_from_settings(void)
{
    const app_settings_t *cur = app_settings_get();
    s_lrt_int = (int)cur->lrt.interval_min;
    s_lrt_ps = (int)cur->lrt.period_start_h;
    s_lrt_pe = (int)cur->lrt.period_end_h;
    s_mqtt_port = (int)(cur->mqtt_port ? cur->mqtt_port : 1883);
    s_idle_min = (int)cur->display_idle_min;
    if (s_mqtt_host_ta) {
        lv_textarea_set_text(s_mqtt_host_ta, cur->mqtt_host);
    }
}

static void build_page(void)
{
    if (s_built || s_parent == NULL) {
        return;
    }
    s_built = true;
    s_kb = NULL;
    s_kb_shade = NULL;
    s_mqtt_host_ta = NULL;
    s_station_btn_lbl = NULL;
    ESP_LOGI(TAG, "building settings page (MQTT + LRT; no weather interval)");

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
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf0f4ff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *body = lv_obj_create(s_page);
    lv_obj_set_size(body, LV_PCT(100), LV_VER_RES - 52);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_pad_row(body, 4, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_elastic(body, false);
    lv_obj_set_scroll_momentum(body, false);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    s_status = lv_label_create(body);
    lv_label_set_text(s_status,
                      "MQTT broker = NAS LAN / Tailscale IP. LRT Interval = poll every N min.");
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9aa4b8), 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);

    s_spinner_count = 0;
    load_draft_from_settings();

    add_section_title(body, "MQTT broker (HA weather)");
    lv_obj_t *host_lbl = lv_label_create(body);
    lv_label_set_text(host_lbl, "Broker IP / host");
    lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(host_lbl, lv_color_hex(0xd1d5db), 0);

    s_mqtt_host_ta = lv_textarea_create(body);
    lv_textarea_set_one_line(s_mqtt_host_ta, true);
    lv_textarea_set_max_length(s_mqtt_host_ta, APP_MQTT_HOST_MAX - 1);
    lv_textarea_set_placeholder_text(s_mqtt_host_ta, "e.g. 192.168.1.50");
    lv_obj_set_width(s_mqtt_host_ta, LV_PCT(100));
    lv_obj_set_style_text_font(s_mqtt_host_ta, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_mqtt_host_ta, ta_focused_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_mqtt_host_ta, ta_focused_cb, LV_EVENT_CLICKED, NULL);
    lv_textarea_set_text(s_mqtt_host_ta, app_settings_get()->mqtt_host);

    add_spinner_row(body, "Broker port", &s_mqtt_port, 1, 65535, "");

    add_section_title(body, "Timetable (LRT)");

    lv_obj_t *st_row = lv_obj_create(body);
    lv_obj_set_width(st_row, LV_PCT(100));
    lv_obj_set_height(st_row, 48);
    lv_obj_set_style_bg_opa(st_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st_row, 0, 0);
    lv_obj_set_style_pad_all(st_row, 0, 0);
    lv_obj_set_flex_flow(st_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(st_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(st_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *st_lbl = lv_label_create(st_row);
    lv_label_set_text(st_lbl, "時間表車站");
    lv_obj_set_style_text_font(st_lbl, font_tc_16(), 0);
    lv_obj_set_style_text_color(st_lbl, lv_color_hex(0xd1d5db), 0);

    lv_obj_t *st_btn = lv_button_create(st_row);
    lv_obj_set_size(st_btn, 180, 40);
    lv_obj_add_event_cb(st_btn, station_pick_btn_cb, LV_EVENT_CLICKED, NULL);
    s_station_btn_lbl = lv_label_create(st_btn);
    lv_obj_set_style_text_font(s_station_btn_lbl, font_tc_16(), 0);
    lv_obj_center(s_station_btn_lbl);
    refresh_station_label();

    add_spinner_row(body, "Interval", &s_lrt_int, 1, 180, "min");
    add_spinner_row(body, "Period start", &s_lrt_ps, 0, 24, "h");
    add_spinner_row(body, "Period end", &s_lrt_pe, 0, 24, "h");

    add_section_title(body, "Display");
    add_spinner_row(body, "Idle dim", &s_idle_min, 0, 180, "min");
    lv_obj_t *idle_hint = lv_label_create(body);
    lv_label_set_text(idle_hint,
                      "0 = never. After N min with no touch: dim backlight and"
                      " pause LRT API until next touch (then fetch immediately).");
    lv_obj_set_width(idle_hint, LV_PCT(100));
    lv_obj_set_style_text_font(idle_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(idle_hint, lv_color_hex(0x6b7280), 0);
    lv_label_set_long_mode(idle_hint, LV_LABEL_LONG_WRAP);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint,
                      "Tip: set broker to Synology NAS IP (same LAN / Tailscale). "
                      "Port default 1883. Weather cadence is controlled by HA MQTT, "
                      "not by a Settings interval.");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7280), 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    lv_obj_t *save = lv_button_create(body);
    lv_obj_set_size(save, 160, 48);
    lv_obj_set_style_margin_top(save, 12, 0);
    lv_obj_t *save_lbl = lv_label_create(save);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save, save_btn_cb, LV_EVENT_CLICKED, NULL);
}

void ui_settings_prepare(lv_obj_t *parent,
                         ui_settings_back_cb_t on_back,
                         ui_settings_applied_cb_t on_applied,
                         void *user)
{
    s_parent = parent;
    s_back_cb = on_back;
    s_applied_cb = on_applied;
    s_user = user;
    s_visible = false;
    s_built = false;
    s_page = NULL;
    s_kb = NULL;
    s_kb_shade = NULL;
    s_mqtt_host_ta = NULL;
    s_station_btn_lbl = NULL;
}

void ui_settings_show(void)
{
    build_page();
    if (s_page == NULL) {
        return;
    }
    load_draft_from_settings();
    refresh_station_label();
    for (int i = 0; i < s_spinner_count; i++) {
        refresh_spinner(&s_spinners[i]);
    }
    lv_label_set_text(s_status,
                      "MQTT broker = NAS LAN / Tailscale IP. LRT Interval = poll every N min.");
    s_visible = true;
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_page);
}

void ui_settings_hide(void)
{
    s_visible = false;
    if (s_page == NULL) {
        return;
    }
    hide_keyboard();
    lv_obj_del(s_page);
    s_page = NULL;
    s_status = NULL;
    s_mqtt_host_ta = NULL;
    s_station_btn_lbl = NULL;
    s_kb = NULL;
    s_kb_shade = NULL;
    s_built = false;
    s_spinner_count = 0;
    memset(s_spinners, 0, sizeof(s_spinners));
}

bool ui_settings_is_visible(void)
{
    return s_visible;
}
