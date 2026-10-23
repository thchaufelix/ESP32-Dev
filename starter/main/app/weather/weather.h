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
#define WEATHER_HOURLY_MAX    4
#define WEATHER_HA_STATE_MAX  32

typedef struct {
    char time_hhmm[WEATHER_HHMM_MAX];
    char condition_en[WEATHER_HA_STATE_MAX];
    char condition_zh[WEATHER_CONDITION_MAX];
    float temp_c;
    bool temp_valid;
    int precip_prob; /* -1 if unknown */
} weather_hourly_t;

typedef struct {
    char place[WEATHER_PLACE_MAX];
    char condition[WEATHER_CONDITION_MAX]; /* prefer zh */
    char condition_en[WEATHER_HA_STATE_MAX];
    char update_hhmm[WEATHER_TIME_MAX];
    char sunrise[WEATHER_HHMM_MAX]; /* unused on HA path; kept empty */
    char sunset[WEATHER_HHMM_MAX];
    float temp_c;
    bool temp_valid;
    int humidity;          /* -1 if unavailable */
    float pressure_hpa;    /* <0 if unavailable */
    float wind_kmh;        /* <0 if unavailable */
    int wind_bearing;      /* -1 if unavailable */
    int icon;              /* legacy HKO; -1 on HA path */
    int hourly_count;
    weather_hourly_t hourly[WEATHER_HOURLY_MAX];
    bool from_mqtt;
} weather_data_t;

/** Parse retained HA MQTT JSON into out. */
esp_err_t weather_parse_ha_mqtt_json(const char *json, weather_data_t *out);

/** Short Traditional Chinese label for HA weather state (fallback). */
const char *weather_ha_state_label_zh(const char *state);

/** Legacy HKO icon label (kept for icon map compat). */
const char *weather_icon_label(int icon);

#ifdef __cplusplus
}
#endif
