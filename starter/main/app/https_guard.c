#include "https_guard.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "https_guard";
static SemaphoreHandle_t s_mu;
static bool s_inited;
static esp_lcd_panel_handle_t s_rgb_panel;

/*
 * With CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC + DYNAMIC_BUFFER, TLS record buffers
 * live in PSRAM. Internal DRAM only needs a few KB for residual SSL/WiFi DMA.
 * Requiring 12 KiB contiguous internal forever-stalled HTTPS when largest
 * plateaued at ~7680 (RGB bounce + WiFi fragmentation).
 */
#define HTTPS_MIN_INTERNAL_HARD   (3 * 1024)  /* refuse below this */
#define HTTPS_MIN_INTERNAL_SOFT   (5 * 1024)  /* prefer; warn if below */
#define HTTPS_MIN_PSRAM_FREE      (256 * 1024)
#define HTTPS_HEAP_POLL_MS        200
#define HTTPS_SETTLE_MS           150

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
    ESP_LOGI(TAG, "HTTPS guard ready (PSRAM TLS; soft internal gate)");
}

void https_guard_set_rgb_panel(esp_lcd_panel_handle_t panel)
{
    /* API compat; PCLK is fixed at panel init — never toggle mid-TLS. */
    s_rgb_panel = panel;
    (void)s_rgb_panel;
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

void https_guard_log_heap(const char *who)
{
    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_all = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "%s: internal free=%u largest=%u psram=%u total_free=%u",
             who ? who : "?",
             (unsigned)free_int, (unsigned)largest,
             (unsigned)free_psram, (unsigned)free_all);
}

void https_guard_post_tls_settle(void)
{
    vTaskDelay(pdMS_TO_TICKS(HTTPS_SETTLE_MS));
}

bool https_guard_internal_heap_tight(void)
{
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largest < HTTPS_MIN_INTERNAL_SOFT) {
        ESP_LOGW(TAG, "internal largest block %u < soft %u",
                 (unsigned)largest, (unsigned)HTTPS_MIN_INTERNAL_SOFT);
        return true;
    }
    return false;
}

bool https_guard_wait_internal_room(int timeout_ms)
{
    int waited = 0;
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    while (true) {
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        if (largest >= HTTPS_MIN_INTERNAL_SOFT) {
            if (waited > 0) {
                ESP_LOGI(TAG, "internal largest OK %u after %d ms",
                         (unsigned)largest, waited);
            }
            return true;
        }

        /* Soft path: EXTERNAL mbedtls puts records in PSRAM; allow try if
         * hard floor met and PSRAM is healthy. Avoids endless 7680<12288 waits. */
        if (largest >= HTTPS_MIN_INTERNAL_HARD && free_psram >= HTTPS_MIN_PSRAM_FREE) {
            ESP_LOGW(TAG, "proceed TLS with largest=%u (soft=%u) psram=%u after %d ms",
                     (unsigned)largest, (unsigned)HTTPS_MIN_INTERNAL_SOFT,
                     (unsigned)free_psram, waited);
            return true;
        }

        if (timeout_ms >= 0 && waited >= timeout_ms) {
            ESP_LOGW(TAG, "wait_internal_room timeout: largest=%u hard=%u soft=%u waited=%d",
                     (unsigned)largest, (unsigned)HTTPS_MIN_INTERNAL_HARD,
                     (unsigned)HTTPS_MIN_INTERNAL_SOFT, waited);
            return false;
        }

        if ((waited % 600) == 0) {
            ESP_LOGW(TAG, "waiting TLS room: largest=%u hard=%u soft=%u (waited %d ms)",
                     (unsigned)largest, (unsigned)HTTPS_MIN_INTERNAL_HARD,
                     (unsigned)HTTPS_MIN_INTERNAL_SOFT, waited);
        }
        vTaskDelay(pdMS_TO_TICKS(HTTPS_HEAP_POLL_MS));
        waited += HTTPS_HEAP_POLL_MS;
        free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
}
