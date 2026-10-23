#include "ha_mqtt.h"

#include <stdio.h>
#include <string.h>

#include "app_settings.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "wifi_manager.h"

static const char *TAG = "ha_mqtt";

#define MQTT_RX_BUF  1536
#define MQTT_OUT_BUF 512

static esp_mqtt_client_handle_t s_client;
static bool s_started;
static bool s_connected;
static char s_host[APP_MQTT_HOST_MAX];
static uint16_t s_port;
static ha_mqtt_weather_cb_t s_cb;
static void *s_cb_user;

static void *psram_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = malloc(n);
    }
    return p;
}

static void handle_weather_payload(const char *data, int len)
{
    if (data == NULL || len <= 0 || len > 4096) {
        return;
    }
    char *buf = psram_alloc((size_t)len + 1);
    if (buf == NULL) {
        ESP_LOGE(TAG, "payload alloc %d failed", len);
        return;
    }
    memcpy(buf, data, (size_t)len);
    buf[len] = '\0';

    weather_data_t *wx = psram_alloc(sizeof(*wx));
    if (wx == NULL) {
        free(buf);
        return;
    }
    esp_err_t err = weather_parse_ha_mqtt_json(buf, wx);
    free(buf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "parse failed: %s", esp_err_to_name(err));
        free(wx);
        return;
    }
    if (s_cb) {
        s_cb(wx, s_cb_user);
    }
    free(wx);
}

static void mqtt_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t ev = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "connected; sub %s", HA_MQTT_TOPIC_WEATHER);
        esp_mqtt_client_subscribe(s_client, HA_MQTT_TOPIC_WEATHER, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_DATA:
        if (ev->topic && ev->topic_len > 0 &&
            (size_t)ev->topic_len == strlen(HA_MQTT_TOPIC_WEATHER) &&
            strncmp(ev->topic, HA_MQTT_TOPIC_WEATHER, (size_t)ev->topic_len) == 0) {
            handle_weather_payload(ev->data, ev->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "mqtt error");
        break;
    default:
        break;
    }
}

static esp_err_t start_with(const char *host, uint16_t port)
{
    if (host == NULL || host[0] == '\0') {
        ESP_LOGW(TAG, "broker host empty — set in Settings");
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_manager_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    char uri[96];
    snprintf(uri, sizeof(uri), "mqtt://%s:%u", host, (unsigned)port);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .buffer.size = MQTT_RX_BUF,
        .buffer.out_size = MQTT_OUT_BUF,
        .network.disable_auto_reconnect = false,
        .session.keepalive = 60,
        .credentials.client_id = "esp32-dev-weather",
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }
    snprintf(s_host, sizeof(s_host), "%s", host);
    s_port = port;
    s_started = true;
    s_connected = false;
    ESP_LOGI(TAG, "started -> %s", uri);
    return ESP_OK;
}

esp_err_t ha_mqtt_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    const app_settings_t *cfg = app_settings_get();
    return start_with(cfg->mqtt_host, cfg->mqtt_port ? cfg->mqtt_port : 1883);
}

esp_err_t ha_mqtt_apply_settings(void)
{
    const app_settings_t *cfg = app_settings_get();
    const char *host = cfg->mqtt_host;
    uint16_t port = cfg->mqtt_port ? cfg->mqtt_port : 1883;

    if (s_started && strcmp(s_host, host) == 0 && s_port == port) {
        ESP_LOGI(TAG, "broker unchanged (%s:%u)", s_host, (unsigned)s_port);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "broker change -> %s:%u (was %s:%u)", host, (unsigned)port,
             s_host[0] ? s_host : "-", (unsigned)s_port);
    ha_mqtt_stop();
    if (!wifi_manager_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    return start_with(host, port);
}

void ha_mqtt_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_started = false;
    s_connected = false;
}

bool ha_mqtt_is_connected(void)
{
    return s_connected;
}

void ha_mqtt_set_weather_cb(ha_mqtt_weather_cb_t cb, void *user)
{
    s_cb = cb;
    s_cb_user = user;
}

esp_err_t ha_mqtt_request_refresh(void)
{
    if (!s_started) {
        esp_err_t e = ha_mqtt_start();
        if (e != ESP_OK) {
            return e;
        }
    }
    if (!s_connected || s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(s_client, HA_MQTT_TOPIC_REFRESH, "1", 1, 0, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}
