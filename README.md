# ESP32-S3-Touch-LCD-7 Development Project

ESP-IDF based development for the [Waveshare ESP32-S3-Touch-LCD-7](https://docs.waveshare.net/ESP32-S3-Touch-LCD-7/).

## Hardware

- Board: Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3N16R8)
- 7" 800×480 RGB IPS capacitive touch LCD (GT911)
- 16 MB Flash + 8 MB PSRAM
- Interfaces: USB, I2C, RS485, CAN, SD card, battery, etc.

## Prerequisites

- ESP-IDF v5.1+ (Waveshare LVGL v9 demo targets ~5.2/5.3; Component Manager required)
- VS Code with ESP-IDF extension (recommended)
- USB Type-C data cable

## Quick Start (WiFi NTP clock on LCD)

1. Copy WiFi credentials (never commit the real file):

```bash
cd starter/main
copy wifi_config.h.example wifi_config.h
# edit WIFI_SSID / WIFI_PASSWORD
```

2. Build and flash (ESP-IDF environment / VS Code ESP-IDF terminal):

```bash
cd starter
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

First build downloads LVGL / esp_lvgl_adapter / GT911 into `managed_components/` (gitignored).

3. Screen shows large `HH:MM:SS` + date (Asia/Shanghai). Serial also prints `CLOCK ...`.

## What's in starter/

- `main/main.c` — WiFi STA + SNTP + LVGL digital clock UI
- `components/waveshare_rgb_lcd_port.*` — RGB LCD + CH422G backlight + GT911 (from Waveshare `09_lvgl_v9_demo`)
- `main/idf_component.yml` — `lvgl ^9`, `esp_lvgl_adapter`, `esp_lcd_touch_gt911`

## Notes

- First flash may require holding BOOT while connecting USB, then release and flash.
- Display uses RGB parallel bus + CH422G IO expander for backlight/touch reset.
- Touch is registered but unused by the clock UI for now.
- Official examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7/tree/main/examples/ESP-IDF

## Resources

- Waveshare Docs: https://docs.waveshare.net/ESP32-S3-Touch-LCD-7/
- Waveshare Examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7
- ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/
