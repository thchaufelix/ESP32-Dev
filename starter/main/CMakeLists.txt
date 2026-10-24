# Re-apply esp_lvgl_adapter fixes after component manager refresh (RGB freeze).
set(_ESP_LVGL_ADAPTER_DIR "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_lvgl_adapter")
if(EXISTS "${_ESP_LVGL_ADAPTER_DIR}/src/display/bridge/v9/lvgl_bridge_v9.c")
    idf_build_get_property(_py PYTHON)
    if(NOT _py)
        set(_py python)
    endif()
    execute_process(
        COMMAND "${_py}" "${CMAKE_SOURCE_DIR}/tools/patch_esp_lvgl_adapter.py" "${_ESP_LVGL_ADAPTER_DIR}"
        RESULT_VARIABLE _adapter_patch_rc
        OUTPUT_VARIABLE _adapter_patch_out
        ERROR_VARIABLE _adapter_patch_err
    )
    if(NOT _adapter_patch_rc EQUAL 0)
        message(WARNING "esp_lvgl_adapter patch failed: ${_adapter_patch_out} ${_adapter_patch_err}")
    else()
        message(STATUS "esp_lvgl_adapter patch: ${_adapter_patch_out}")
    endif()
endif()
idf_component_register(
    SRCS
        "main.c"
        "app/wifi/wifi_manager.c"
        "app/https_guard.c"
        "app/ui/ui_clock.c"
        "app/ui/ui_topbar.c"
        "app/ui/ui_wifi_settings.c"
        "app/ui/ui_settings.c"
        "app/ui/ui_lrt_station_picker.c"
        "app/settings/app_settings.c"
        "app/ui/ui_lrt_schedule.c"
        "app/lrt/lrt_schedule.c"
        "app/lrt/lrt_stations.c"
        "app/weather/weather.c"
        "app/mqtt/ha_mqtt.c"
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
        "assets/weather_icons/wx_icon_snow.c"
        "assets/weather_icons/wx_icon_snowy_rainy.c"
        "assets/weather_icons/wx_icon_hail.c"
        "assets/weather_icons/wx_icon_exceptional.c"
        "assets/weather_icons/wx_icon_generic.c"
        "assets/lrt/icon_train_car.c"
        "assets/lrt/icon_train_station.c"
        "assets/ui/icon_temp_c.c"
        "assets/ui/icon_rain_drop.c"
        "assets/ui/icon_sun_up.c"
        "assets/ui/icon_sun_down.c"
        "assets/ui/icon_settings.c"
        "assets/ui/icon_speed.c"
        "assets/fonts/font_genwan_tc_20.c"
        "assets/fonts/fonts.c"
        "../components/waveshare_rgb_lcd_port.c"
    INCLUDE_DIRS
        "."
        "app"
        "app/ui"
        "app/lrt"
        "app/weather"
        "app/mqtt"
        "app/wifi"
        "app/settings"
        "assets/weather_icons"
        "assets/lrt"
        "assets/ui"
        "assets/fonts"
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
        espressif__mqtt
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
