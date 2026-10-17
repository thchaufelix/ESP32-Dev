#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "starter";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3-Touch-LCD-7 Starter");
    ESP_LOGI(TAG, "Ready for Waveshare LCD / LVGL / custom gadget development");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Alive");
    }
}
