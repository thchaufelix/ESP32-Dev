#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Idle display dimmer: after display_idle_min with no LVGL pointer input,
 * turn CH422G backlight off and pause LRT API fetches. Any touch restores
 * backlight immediately, resumes LRT timer, and triggers an immediate fetch.
 * The wake touch is ignored as UI input (stop_processing + wait_release) so
 * it does not click buttons / rows under the finger.
 * Call once from ui_app_init on the LVGL task after UI is built.
 */
esp_err_t ui_idle_init(void);

/** Re-read idle timeout from app_settings (after Save). LVGL task. */
void ui_idle_apply_settings(void);

/** True while backlight is dimmed / LRT paused for idle. */
bool ui_idle_is_dimmed(void);

#ifdef __cplusplus
}
#endif
