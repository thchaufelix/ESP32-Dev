idf_component_register(
    SRCS
        "main.c"
        "app/wifi/wifi_manager.c"
        "app/ui/ui_clock.c"
        "app/ui/ui_topbar.c"
        "app/ui/ui_wifi_settings.c"
        "app/ui/ui_lrt_schedule.c"
        "app/lrt/lrt_schedule.c"
        "app/weather/weather.c"
        "app/ui/ui_weather.c"
        "app/ui/ui_app.c"
        "assets/weather_icons/wx_icon_map.c"
        "assets/weather_icons/wx_icon_sunny.c"
        "assets/weather_icons/wx_icon_partly_cloudy.c"
        "assets/weather_icons/wx_icon_cloudy.c"
        "assets/weather_icons/wx_icon_overcast.c"
        "assets/weather_icons/wx_icon_rain.c"
        "assets/weather_icons/wx_icon_heavy_rain.c"
        "assets/weather_icons/wx_icon_thunderstorm.c"
        "assets/weather_icons/wx_icon_clear_night.c"
        "assets/weather_icons/wx_icon_cloudy_night.c"
        "assets/weather_icons/wx_icon_windy.c"
        "assets/weather_icons/wx_icon_foggy.c"
        "assets/weather_icons/wx_icon_hot.c"
        "assets/weather_icons/wx_icon_cold.c"
        "assets/weather_icons/wx_icon_generic.c"
        "../components/waveshare_rgb_lcd_port.c"
    INCLUDE_DIRS
        "."
        "app"
        "app/ui"
        "app/lrt"
        "app/weather"
        "app/wifi"
        "assets/weather_icons"
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