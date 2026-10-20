#include "ui_clock.h"

#include "ui_topbar.h"
#include "ui_weather.h"

/**
 * Legacy clock panel removed from the right side.
 * Time lives in the top bar; status line lives on the weather panel.
 * These wrappers keep ui_app call sites stable.
 */

void ui_clock_create(lv_obj_t *parent)
{
    (void)parent;
}

void ui_clock_set_status_unlocked(const char *text)
{
    ui_weather_set_status_unlocked(text);
}

void ui_clock_set_status(const char *text)
{
    ui_weather_set_status(text);
}

void ui_clock_set_time_ready(bool ready)
{
    ui_topbar_set_time_ready(ready);
}

lv_obj_t *ui_clock_get_screen(void)
{
    return NULL;
}