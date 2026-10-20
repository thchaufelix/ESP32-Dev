#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create split UI: top bar, left LRT schedule, right clock, wifi settings. */
esp_err_t ui_app_init(void);

#ifdef __cplusplus
}
#endif
