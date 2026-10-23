#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "weather.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Retained JSON from Home Assistant (see docs/ha/esp_weather_mqtt.yaml). */
#define HA_MQTT_TOPIC_WEATHER "ha/weather/home"
/** ESP publishes "1" here; HA automation republishes weather. */
#define HA_MQTT_TOPIC_REFRESH "ha/weather/home/refresh"

typedef void (*ha_mqtt_weather_cb_t)(const weather_data_t *data, void *user);

/**
 * Start MQTT using broker host/port from app_settings (NVS).
 * Idempotent. Call after WiFi is up.
 */
esp_err_t ha_mqtt_start(void);

/**
 * Re-read app_settings; if host/port changed (or not connected), stop+start.
 * Call after Settings Save.
 */
esp_err_t ha_mqtt_apply_settings(void);

void ha_mqtt_stop(void);
bool ha_mqtt_is_connected(void);
void ha_mqtt_set_weather_cb(ha_mqtt_weather_cb_t cb, void *user);
esp_err_t ha_mqtt_request_refresh(void);

#ifdef __cplusplus
}
#endif
