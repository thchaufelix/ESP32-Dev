#include "ui_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_settings.h"
#include "esp_log.h"

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
static ui_settings_back_cb_t s_back_cb;
static ui_settings_applied_cb_t s_applied_cb;
static void *s_user;
static bool s_visible;
static bool s_built;

static spinner_ctx_t s_spinners[6];
static int s_spinner_count;

/* Spinner value storage (int* for +/- rows). */
static int s_wx_int;
static int s_wx_ps;
static int s_wx_pe;
static int s_lrt_int;
static int s_lrt_ps;
static int s_lrt_pe;


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

static void back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_back_cb) {
        s_back_cb(s_user);
    }
}

static void save_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    app_settings_t out = {0};
    out.weather.interval_min = (uint16_t)s_wx_int;
    out.weather.period_start_h = (uint8_t)s_wx_ps;
    out.weather.period_end_h = (uint8_t)s_wx_pe;
    out.lrt.interval_min = (uint16_t)s_lrt_int;
    out.lrt.period_start_h = (uint8_t)s_lrt_ps;
    out.lrt.period_end_h = (uint8_t)s_lrt_pe;

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
    s_wx_int = (int)cur->weather.interval_min;
    s_wx_ps = (int)cur->weather.period_start_h;
    s_wx_pe = (int)cur->weather.period_end_h;
    s_lrt_int = (int)cur->lrt.interval_min;
    s_lrt_ps = (int)cur->lrt.period_start_h;
    s_lrt_pe = (int)cur->lrt.period_end_h;
}


static void build_page(void)
{
    if (s_built || s_parent == NULL) {
        return;
    }
    s_built = true;
    ESP_LOGI(TAG, "building update-interval settings page");

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
                      "Interval = poll every N min. Period = active hours (start==end => always).");
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9aa4b8), 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);

    s_spinner_count = 0;
    load_draft_from_settings();

    add_section_title(body, "Weather");
    add_spinner_row(body, "Interval", &s_wx_int, 1, 180, "min");
    add_spinner_row(body, "Period start", &s_wx_ps, 0, 24, "h");
    add_spinner_row(body, "Period end", &s_wx_pe, 0, 24, "h");

    add_section_title(body, "Timetable (LRT)");
    add_spinner_row(body, "Interval", &s_lrt_int, 1, 180, "min");
    add_spinner_row(body, "Period start", &s_lrt_ps, 0, 24, "h");
    add_spinner_row(body, "Period end", &s_lrt_pe, 0, 24, "h");

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Tip: period 0-24 = all day. Wrap e.g. 22-6 for overnight.");
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
}

void ui_settings_show(void)
{
    build_page();
    if (s_page == NULL) {
        return;
    }
    load_draft_from_settings();
    /* Refresh spinner labels if already built */
    s_wx_int = (int)app_settings_get()->weather.interval_min;
    s_wx_ps = (int)app_settings_get()->weather.period_start_h;
    s_wx_pe = (int)app_settings_get()->weather.period_end_h;
    s_lrt_int = (int)app_settings_get()->lrt.interval_min;
    s_lrt_ps = (int)app_settings_get()->lrt.period_start_h;
    s_lrt_pe = (int)app_settings_get()->lrt.period_end_h;
    for (int i = 0; i < s_spinner_count; i++) {
        refresh_spinner(&s_spinners[i]);
    }
    lv_label_set_text(s_status,
                      "Interval = poll every N min. Period = active hours (start==end => always).");
    s_visible = true;
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_page);
}

void ui_settings_hide(void)
{
    if (s_page == NULL) {
        s_visible = false;
        return;
    }
    s_visible = false;
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
}

bool ui_settings_is_visible(void)
{
    return s_visible;
}
