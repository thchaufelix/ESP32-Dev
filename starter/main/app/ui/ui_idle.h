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
 * Idle / deep-idle display power saving.
 *
 * After display_idle_min with no LVGL pointer input:
 *   backlight off, pause LRT fetches, stop RGB panel (PCLK/DMA).
 *
 * After display_deep_min (must be > idle; 0 = off), still no activity:
 *   stop MQTT + suspend WiFi, then light-sleep poll touch (closest practical —
 *   GT911 INT is not wired on this board, so chip deep-sleep cannot wake on touch).
 *
 * Wake (touch): recreate RGB, backlight on, resume WiFi (+ wait connected),
 * start MQTT, resume LRT + immediate fetch. Wake gesture ignored as UI.
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

/** Re-read idle/deep timeouts from app_settings (after Save). LVGL task. */
void ui_idle_apply_settings(void);

/** True while idle (backlight off / LRT paused; RGB may still be tearing down). */
bool ui_idle_is_dimmed(void);

/** True while in deep idle (WiFi/MQTT stopped). */
bool ui_idle_is_deep(void);

#ifdef __cplusplus
}
#endif
