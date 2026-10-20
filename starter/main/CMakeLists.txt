idf_component_register(
    SRCS
        "main.c"
        "app/wifi_manager.c"
        "app/ui_clock.c"
        "app/ui_topbar.c"
        "app/ui_wifi_settings.c"
        "app/ui_lrt_schedule.c"
        "app/lrt_schedule.c"
        "app/weather.c"
        "app/ui_weather.c"
        "app/ui_app.c"
        "../components/waveshare_rgb_lcd_port.c"
    INCLUDE_DIRS
        "."
        "app"
        "../components"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_http_client
        espressif__cjson
        mbedtls
)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
elseif("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
endif()

if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()