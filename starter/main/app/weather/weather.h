#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_PLACE_MAX     32
#define WEATHER_CONDITION_MAX 48
#define WEATHER_TIME_MAX      24
#define WEATHER_HHMM_MAX      8

typedef struct {
    char place[WEATHER_PLACE_MAX];
    char condition[WEATHER_CONDITION_MAX];
    char update_hhmm[WEATHER_TIME_MAX];
    char sunrise[WEATHER_HHMM_MAX]; /* HH:MM, empty if unknown */
    char sunset[WEATHER_HHMM_MAX];
    int temp_c;
    bool temp_valid;
    int humidity; /* -1 if unavailable (HKO HQ only when present) */
    int icon;
} weather_data_t;

/**
 * HTTPS fetch + parse HKO current weather (rhrread, English) and today's
 * sunrise/sunset (SRS). Prefers temperature place "Tuen Mun".
 * Blocking; call from a worker task, not the LVGL task.
 */
esp_err_t weather_fetch(weather_data_t *out);

/** Short Traditional Chinese label for an HKO weather icon code. */
const char *weather_icon_label(int icon);

#ifdef __cplusplus
}
#endif