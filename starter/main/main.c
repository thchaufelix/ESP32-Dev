#include <assert.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "waveshare_rgb_lcd_port.h"

#include "https_guard.h"
#include "ui_app.h"
#include "ui_idle.h"
#include "wifi_manager.h"

static const char *TAG = "main";

static esp_lcd_panel_handle_t s_panel_handle;
static esp_lcd_touch_handle_t s_touch_handle;
static lv_display_t *s_disp;
static const esp_lv_adapter_rotation_t s_rotation = ESP_LV_ADAPTER_ROTATE_0;
static const esp_lv_adapter_tear_avoid_mode_t s_tear_mode =
    ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_FULL;

static void log_heap(const char *where)
{
    ESP_LOGI(TAG, "%s: free_internal=%u largest_internal=%u free_psram=%u",
             where,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void display_init(void)
{
    /* DEFAULT_RGB is TRIPLE_PARTIAL: needs 80KB INTERNAL partial draw buf (fails after
     * WiFi+RGB bounce) and still needs INTERNAL bridge for RGB ISR user_ctx.
     * DOUBLE_FULL uses panel FBs in PSRAM; bridge INTERNAL via adapter patch. */

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        s_tear_mode,
        s_rotation,
        &s_panel_handle,
        &s_touch_handle));
    https_guard_init();
    https_guard_set_rgb_panel(s_panel_handle);
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    /* PSRAM stack frees internal DMA RAM for WiFi + RGB bounce */
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        s_panel_handle,
        NULL,
        EXAMPLE_LCD_H_RES,
        EXAMPLE_LCD_V_RES,
        s_rotation);
    disp_config.tear_avoid_mode = s_tear_mode;
    disp_config.profile.use_psram = true;

    s_disp = esp_lv_adapter_register_display(&disp_config);
    assert(s_disp != NULL);

    if (s_touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_disp, s_touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
        ESP_LOGI(TAG, "GT911 touch registered");
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    ui_idle_bind(&s_panel_handle, s_touch_handle, s_disp, s_tear_mode, s_rotation);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 Touch LCD 7 - WiFi OOM fix");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    log_heap("boot");

    /* Claim WiFi driver RAM BEFORE RGB framebuffers / bounce buffers */
    ret = wifi_manager_early_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_manager_early_init failed: %s", esp_err_to_name(ret));
    }

    display_init();
    log_heap("after display");

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ESP_ERROR_CHECK(ui_app_init());
        esp_lv_adapter_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_manager_start failed: %s", esp_err_to_name(ret));
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}