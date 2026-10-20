#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create clock UI, top bar, wifi settings page; register wifi status callbacks. */
esp_err_t ui_app_init(void);

#ifdef __cplusplus
}
#endif
