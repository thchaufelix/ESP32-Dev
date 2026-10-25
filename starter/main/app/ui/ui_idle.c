#include "ui_idle.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_settings.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "https_guard.h"
#include "lvgl.h"
#include "ui_lrt_schedule.h"
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "ui_idle";

typedef enum {
    IDLE_CMD_ENTER = 1,
    IDLE_CMD_WAKE = 2,
} idle_cmd_t;

static lv_timer_t *s_check_timer;
static uint32_t s_last_activity_ms;
static volatile bool s_dimmed;
static volatile bool s_wake_req;
static bool s_indev_hooked;
static bool s_rgb_off;
static bool s_bound;

static esp_lcd_panel_handle_t *s_panel_slot;
static esp_lcd_touch_handle_t s_touch;
static lv_display_t *s_disp;
static esp_lv_adapter_tear_avoid_mode_t s_tear_mode;
static esp_lv_adapter_rotation_t s_rotation;

static QueueHandle_t s_cmd_q;
static TaskHandle_t s_worker;

static uint32_t now_ms(void)
{
    return lv_tick_get();
}

static void note_activity(void)
{
    s_last_activity_ms = now_ms();
}

static bool touch_pressed(void)
{
    if (s_touch == NULL) {
        return false;
    }
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        return false;
    }
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t strength = 0;
    uint8_t n = 0;
    if (!esp_lcd_touch_get_coordinates(s_touch, &x, &y, &strength, &n, 1)) {
        return false;
    }
    return n > 0;
}

static void wait_touch_release(void)
{
    for (int i = 0; i < 100; i++) {
        if (!touch_pressed()) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t rgb_stop(void)
{
    if (s_rgb_off) {
        return ESP_OK;
    }
    if (s_panel_slot == NULL || *s_panel_slot == NULL || s_disp == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_lv_adapter_sleep_prepare();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sleep_prepare failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_del(*s_panel_slot);
    *s_panel_slot = NULL;
    https_guard_set_rgb_panel(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "panel_del failed: %s", esp_err_to_name(err));
        esp_lcd_panel_handle_t panel = NULL;
        if (waveshare_esp32_s3_rgb_panel_new(s_tear_mode, s_rotation, &panel) == ESP_OK) {
            *s_panel_slot = panel;
            https_guard_set_rgb_panel(panel);
            (void)esp_lv_adapter_sleep_recover(s_disp, panel, NULL);
        }
        return err;
    }

    s_rgb_off = true;
    ESP_LOGI(TAG, "idle: RGB panel deleted (PCLK/DMA stopped)");
    return ESP_OK;
}

static esp_err_t rgb_start(void)
{
    if (!s_rgb_off) {
        return ESP_OK;
    }
    if (s_panel_slot == NULL || s_disp == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_err_t err = waveshare_esp32_s3_rgb_panel_new(s_tear_mode, s_rotation, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rgb_panel_new failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lv_adapter_sleep_recover(s_disp, panel, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sleep_recover failed: %s", esp_err_to_name(err));
        (void)esp_lcd_panel_del(panel);
        return err;
    }

    *s_panel_slot = panel;
    https_guard_set_rgb_panel(panel);
    s_rgb_off = false;
    ESP_LOGI(TAG, "wake: RGB panel restored");
    return ESP_OK;
}

static void finish_wake(bool fetch_now)
{
    note_activity();
    s_wake_req = false;
    s_dimmed = false;

    esp_err_t err = waveshare_rgb_lcd_backlight_on();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight_on failed: %s", esp_err_to_name(err));
    }

    if (esp_lv_adapter_lock(2000) == ESP_OK) {
        ui_lrt_schedule_set_fetch_paused(false);
        if (fetch_now) {
            ui_lrt_schedule_request_refresh();
        }
        esp_lv_adapter_unlock();
    } else {
        ESP_LOGW(TAG, "LRT resume: LVGL lock timeout");
    }
    ESP_LOGI(TAG, "wake: backlight on + LRT resumed%s",
             fetch_now ? " + immediate fetch" : "");
}

static bool drain_wake_cmds(void)
{
    bool wake = s_wake_req;
    idle_cmd_t cmd;
    while (xQueueReceive(s_cmd_q, &cmd, 0) == pdTRUE) {
        if (cmd == IDLE_CMD_WAKE) {
            wake = true;
        }
    }
    if (s_wake_req) {
        wake = true;
    }
    return wake;
}

static void idle_worker(void *arg)
{
    (void)arg;
    idle_cmd_t cmd;

    for (;;) {
        if (xQueueReceive(s_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (cmd == IDLE_CMD_WAKE) {
            /* Spurious wake with no enter in flight. */
            if (s_dimmed && !s_rgb_off) {
                finish_wake(true);
            }
            continue;
        }

        if (cmd != IDLE_CMD_ENTER || !s_dimmed) {
            continue;
        }

        if (drain_wake_cmds()) {
            /* Cancelled before RGB teardown. */
            finish_wake(true);
            continue;
        }

        if (rgb_stop() != ESP_OK) {
            /* LVGL still running: indev path can request wake. */
            ESP_LOGW(TAG, "RGB stop failed; stay dimmed with RGB up");
            continue;
        }

        if (drain_wake_cmds()) {
            wait_touch_release();
            if (rgb_start() == ESP_OK) {
                finish_wake(true);
            }
            continue;
        }

        while (s_dimmed && s_rgb_off) {
            if (touch_pressed() || s_wake_req) {
                bool from_touch = touch_pressed();
                if (from_touch) {
                    wait_touch_release();
                }
                if (rgb_start() == ESP_OK) {
                    finish_wake(true);
                } else {
                    ESP_LOGE(TAG, "wake RGB restore failed; stay dark");
                }
                break;
            }

            idle_cmd_t extra;
            if (xQueueReceive(s_cmd_q, &extra, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (extra == IDLE_CMD_WAKE || s_wake_req) {
                    if (rgb_start() == ESP_OK) {
                        finish_wake(false);
                    }
                    break;
                }
            }
        }
    }
}

static void queue_cmd(idle_cmd_t cmd)
{
    if (s_cmd_q == NULL) {
        return;
    }
    (void)xQueueSend(s_cmd_q, &cmd, 0);
}

static void enter_idle(void)
{
    if (s_dimmed) {
        return;
    }
    s_wake_req = false;
    s_dimmed = true;
    ui_lrt_schedule_set_fetch_paused(true);
    esp_err_t err = waveshare_rgb_lcd_backlight_off();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight_off failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "idle: backlight off + LRT paused; stopping RGB");
    }
    queue_cmd(IDLE_CMD_ENTER);
}

static void request_wake_from_ui(bool fetch_hint)
{
    (void)fetch_hint;
    note_activity();
    if (!s_dimmed) {
        return;
    }
    /* After ENTER is queued, worker owns backlight / RGB restore so we do not
     * race sleep_prepare / panel_del. */
    s_wake_req = true;
    queue_cmd(IDLE_CMD_WAKE);
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
        request_wake_from_ui(true);
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
        if (s_dimmed) {
            request_wake_from_ui(false);
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

void ui_idle_bind(esp_lcd_panel_handle_t *panel_slot,
                  esp_lcd_touch_handle_t touch,
                  lv_display_t *disp,
                  esp_lv_adapter_tear_avoid_mode_t tear_mode,
                  esp_lv_adapter_rotation_t rotation)
{
    s_panel_slot = panel_slot;
    s_touch = touch;
    s_disp = disp;
    s_tear_mode = tear_mode;
    s_rotation = rotation;
    s_bound = (panel_slot != NULL && disp != NULL);
    ESP_LOGI(TAG, "bound panel_slot=%p touch=%p disp=%p",
             (void *)panel_slot, (void *)touch, (void *)disp);
}

esp_err_t ui_idle_init(void)
{
    s_dimmed = false;
    s_wake_req = false;
    s_rgb_off = false;
    s_indev_hooked = false;
    note_activity();
    hook_indevs();

    if (s_cmd_q == NULL) {
        s_cmd_q = xQueueCreate(4, sizeof(idle_cmd_t));
    }
    if (s_worker == NULL && s_cmd_q != NULL && s_bound) {
        BaseType_t ok = xTaskCreate(idle_worker, "idle_rgb", 4096, NULL, 5, &s_worker);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "idle_rgb task create failed");
            s_worker = NULL;
        }
    }

    if (s_check_timer == NULL) {
        s_check_timer = lv_timer_create(idle_check_cb, 1000, NULL);
    }
    ESP_LOGI(TAG, "init (idle timeout %u min, rgb_pause=%d)",
             (unsigned)app_settings_get()->display_idle_min, (int)s_bound);
    return ESP_OK;
}

void ui_idle_apply_settings(void)
{
    uint32_t idle_ms = app_settings_display_idle_ms();
    ESP_LOGI(TAG, "apply_settings idle=%u ms (dimmed=%d rgb_off=%d)",
             (unsigned)idle_ms, (int)s_dimmed, (int)s_rgb_off);
    note_activity();
    if (idle_ms == 0 && s_dimmed) {
        request_wake_from_ui(false);
    }
}

bool ui_idle_is_dimmed(void)
{
    return s_dimmed;
}