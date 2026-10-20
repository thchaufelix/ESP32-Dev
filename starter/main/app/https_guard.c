#include "https_guard.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "https_guard";
static SemaphoreHandle_t s_mu;
static bool s_inited;

void https_guard_init(void)
{
    if (s_inited) {
        return;
    }
    s_mu = xSemaphoreCreateMutex();
    if (s_mu == NULL) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }
    s_inited = true;
    ESP_LOGI(TAG, "HTTPS guard ready");
}

bool https_guard_lock(int timeout_ms)
{
    if (!s_inited || s_mu == NULL) {
        https_guard_init();
        if (s_mu == NULL) {
            return false;
        }
    }
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS((uint32_t)timeout_ms);
    if (xSemaphoreTake(s_mu, ticks) != pdTRUE) {
        ESP_LOGW(TAG, "lock timeout (%d ms)", timeout_ms);
        return false;
    }
    return true;
}

void https_guard_unlock(void)
{
    if (s_mu) {
        xSemaphoreGive(s_mu);
    }
}
