#include <assert.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "waveshare_rgb_lcd_port.h"

#include "ui_app.h"
#include "wifi_manager.h"

static const char *TAG = "main";

static void display_init(void)
{
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        tear_mode,
        rotation,
        &panel_handle,
        &touch_handle));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    /* Larger stack; keep in internal RAM for WiFi+LVGL stability */
    adapter_config.task_stack_size = 16 * 1024;
    adapter_config.stack_in_psram = false;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle,
        NULL,
        EXAMPLE_LCD_H_RES,
        EXAMPLE_LCD_V_RES,
        rotation);
    disp_config.profile.use_psram = true;

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_config);
    assert(disp != NULL);

    if (touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
        ESP_LOGI(TAG, "GT911 touch registered");
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 Touch LCD 7 - WiFi settings UI (stable boot)");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* UI first — never scan on boot; WiFi after screen is alive */
    display_init();

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ESP_ERROR_CHECK(ui_app_init());
        esp_lv_adapter_unlock();
    }

    /* Let LVGL paint once before bringing up RF */
    vTaskDelay(pdMS_TO_TICKS(300));

    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_manager_init failed: %s", esp_err_to_name(ret));
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
