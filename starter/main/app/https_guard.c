#include "https_guard.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "https_guard";
static SemaphoreHandle_t s_mu;
static bool s_inited;
static esp_lcd_panel_handle_t s_rgb_panel;
static int s_pclk_depth; /* nested PCLK-safe depth under lock */

/* With DYNAMIC_BUFFER peak is lower than full IN+OUT, but fragmented internal
 * DRAM still fails when the largest contiguous block is tiny. */
#define HTTPS_MIN_INTERNAL_LARGEST  (10 * 1024)
#define HTTPS_HEAP_POLL_MS          120
#define HTTPS_SETTLE_MS             150
/* FAQ: drop PCLK ~20 ms before heavy flash/WiFi so one RGB frame finishes. */
#define HTTPS_PCLK_SETTLE_MS        25

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

void https_guard_set_rgb_panel(esp_lcd_panel_handle_t panel)
{
    s_rgb_panel = panel;
}

static void rgb_pclk_enter_safe(void)
{
    if (s_rgb_panel == NULL) {
        return;
    }
    if (s_pclk_depth++ == 0) {
        /* Espressif FAQ: lower PCLK around flash/WiFi to avoid RGB overall drift. */
        esp_err_t err = esp_lcd_rgb_panel_set_pclk(s_rgb_panel, EXAMPLE_LCD_PIXEL_CLOCK_HZ_SAFE);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "set_pclk safe failed: %s", esp_err_to_name(err));
        } else {
            vTaskDelay(pdMS_TO_TICKS(HTTPS_PCLK_SETTLE_MS));
        }
    }
}

static void rgb_pclk_leave_safe(void)
{
    if (s_rgb_panel == NULL) {
        return;
    }
    if (s_pclk_depth <= 0) {
        s_pclk_depth = 0;
        return;
    }
    if (--s_pclk_depth == 0) {
        esp_err_t err = esp_lcd_rgb_panel_set_pclk(s_rgb_panel, EXAMPLE_LCD_PIXEL_CLOCK_HZ);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "set_pclk restore failed: %s", esp_err_to_name(err));
        }
    }
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
    rgb_pclk_enter_safe();
    return true;
}

void https_guard_unlock(void)
{
    rgb_pclk_leave_safe();
    if (s_mu) {
        xSemaphoreGive(s_mu);
    }
}

void https_guard_log_heap(const char *who)
{
    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_all = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "%s: internal free=%u largest=%u total_free=%u",
             who ? who : "?", (unsigned)free_int, (unsigned)largest, (unsigned)free_all);
}

void https_guard_post_tls_settle(void)
{
    /* Let FreeRTOS idle / other tasks finish freeing; reduces back-to-back ssl_setup flakes. */
    vTaskDelay(pdMS_TO_TICKS(HTTPS_SETTLE_MS));
}

bool https_guard_internal_heap_tight(void)
{
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largest < HTTPS_MIN_INTERNAL_LARGEST) {
        ESP_LOGW(TAG, "internal largest block %u < %u (TLS may -0x008D)",
                 (unsigned)largest, (unsigned)HTTPS_MIN_INTERNAL_LARGEST);
        return true;
    }
    return false;
}

bool https_guard_wait_internal_room(int timeout_ms)
{
    int waited = 0;
    while (true) {
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (largest >= HTTPS_MIN_INTERNAL_LARGEST) {
            if (waited > 0) {
                ESP_LOGI(TAG, "internal largest recovered to %u after %d ms",
                         (unsigned)largest, waited);
            }
            return true;
        }
        if (timeout_ms >= 0 && waited >= timeout_ms) {
            ESP_LOGW(TAG, "wait_internal_room timeout: largest=%u need=%u waited=%d",
                     (unsigned)largest, (unsigned)HTTPS_MIN_INTERNAL_LARGEST, waited);
            return false;
        }
        if ((waited % 480) == 0) {
            ESP_LOGW(TAG, "waiting for TLS room: largest=%u need=%u (waited %d ms)",
                     (unsigned)largest, (unsigned)HTTPS_MIN_INTERNAL_LARGEST, waited);
        }
        vTaskDelay(pdMS_TO_TICKS(HTTPS_HEAP_POLL_MS));
        waited += HTTPS_HEAP_POLL_MS;
    }
}
