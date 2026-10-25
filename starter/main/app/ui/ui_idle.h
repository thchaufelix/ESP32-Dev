#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Idle display dimmer: after display_idle_min with no LVGL pointer input,
 * turn CH422G backlight off, pause LRT API fetches, and stop the RGB panel
 * (esp_lv_adapter_sleep_prepare + esp_lcd_panel_del) so PCLK/DMA/bounce stop.
 *
 * Wake (touch while RGB off is polled on a worker task): recreate RGB panel,
 * esp_lv_adapter_sleep_recover, then backlight on, resume LRT + immediate fetch.
 * The wake gesture is ignored as UI input (wait-for-release before LVGL resumes;
 * if still dimmed with RGB on, indev stop_processing + wait_release).
 *
 * Call ui_idle_bind() from main after display init, then ui_idle_init() from
 * ui_app_init on the LVGL task after UI is built.
 */
void ui_idle_bind(esp_lcd_panel_handle_t *panel_slot,
                  esp_lcd_touch_handle_t touch,
                  lv_display_t *disp,
                  esp_lv_adapter_tear_avoid_mode_t tear_mode,
                  esp_lv_adapter_rotation_t rotation);

esp_err_t ui_idle_init(void);

/** Re-read idle timeout from app_settings (after Save). LVGL task. */
void ui_idle_apply_settings(void);

/** True while idle (backlight off / LRT paused; RGB may still be tearing down). */
bool ui_idle_is_dimmed(void);

#ifdef __cplusplus
}
#endif