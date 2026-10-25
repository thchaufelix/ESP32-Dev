#include "ui_idle.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_settings.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_lrt_schedule.h"
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "ui_idle";

static lv_timer_t *s_check_timer;
static uint32_t s_last_activity_ms;
static bool s_dimmed;
static bool s_indev_hooked;

static uint32_t now_ms(void)
{
    return lv_tick_get();
}

static void note_activity(void)
{
    s_last_activity_ms = now_ms();
}

static void enter_idle(void)
{
    if (s_dimmed) {
        return;
    }
    s_dimmed = true;
    ui_lrt_schedule_set_fetch_paused(true);
    esp_err_t err = waveshare_rgb_lcd_backlight_off();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight_off failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "idle: backlight off + LRT paused");
    }
}

static void leave_idle(bool fetch_now)
{
    note_activity();
    if (!s_dimmed) {
        return;
    }
    s_dimmed = false;
    esp_err_t err = waveshare_rgb_lcd_backlight_on();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight_on failed: %s", esp_err_to_name(err));
    }
    ui_lrt_schedule_set_fetch_paused(false);
    if (fetch_now) {
        ui_lrt_schedule_request_refresh();
    }
    ESP_LOGI(TAG, "wake: backlight on + LRT resumed%s",
             fetch_now ? " + immediate fetch" : "");
}

static void indev_activity_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_CLICKED &&
        code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED &&
        code != LV_EVENT_SCROLL) {
        return;
    }
    if (s_dimmed) {
        /* First touch only wakes: backlight + LRT fetch. Do not deliver this
         * gesture to widgets underneath (buttons / rows would otherwise fire). */
        leave_idle(true);
        lv_indev_t *indev = lv_indev_active();
        if (indev == NULL) {
            indev = (lv_indev_t *)lv_event_get_target(e);
        }
        if (indev != NULL) {
            lv_indev_stop_processing(indev);
            lv_indev_wait_release(indev);
        }
        return;
    }
    note_activity();
}

static void hook_indevs(void)
{
    if (s_indev_hooked) {
        return;
    }
    lv_indev_t *indev = NULL;
    int n = 0;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            continue;
        }
        lv_indev_add_event_cb(indev, indev_activity_cb, LV_EVENT_ALL, NULL);
        n++;
    }
    s_indev_hooked = (n > 0);
    ESP_LOGI(TAG, "hooked %d pointer indev(s)", n);
}

static void idle_check_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t idle_ms = app_settings_display_idle_ms();
    if (idle_ms == 0) {
        /* Disabled: ensure we are awake. */
        if (s_dimmed) {
            leave_idle(false);
        } else {
            note_activity();
        }
        return;
    }
    if (s_dimmed) {
        return;
    }
    uint32_t elapsed = now_ms() - s_last_activity_ms;
    if (elapsed >= idle_ms) {
        enter_idle();
    }
}

esp_err_t ui_idle_init(void)
{
    s_dimmed = false;
    s_indev_hooked = false;
    note_activity();
    hook_indevs();
    if (s_check_timer == NULL) {
        s_check_timer = lv_timer_create(idle_check_cb, 1000, NULL);
    }
    ESP_LOGI(TAG, "init (idle timeout %u min)",
             (unsigned)app_settings_get()->display_idle_min);
    return ESP_OK;
}

void ui_idle_apply_settings(void)
{
    uint32_t idle_ms = app_settings_display_idle_ms();
    ESP_LOGI(TAG, "apply_settings idle=%u ms (dimmed=%d)",
             (unsigned)idle_ms, (int)s_dimmed);
    note_activity();
    if (idle_ms == 0 && s_dimmed) {
        leave_idle(false);
    }
}

bool ui_idle_is_dimmed(void)
{
    return s_dimmed;
}
