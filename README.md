# ESP32-S3-Touch-LCD-7 Development Project

ESP-IDF based development for the [Waveshare ESP32-S3-Touch-LCD-7](https://docs.waveshare.net/ESP32-S3-Touch-LCD-7/).

## Hardware

- Board: Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3N16R8)
- 7" 800×480 RGB IPS capacitive touch LCD (GT911)
- 16 MB Flash + 8 MB PSRAM
- Interfaces: USB, I2C, RS485, CAN, SD card, battery, etc.

## Prerequisites

- ESP-IDF v5.x (recommended matching Waveshare examples, e.g. 5.3+)
- VS Code with ESP-IDF extension (recommended)
- USB Type-C data cable

## Quick Start

1. Install ESP-IDF. Follow Waveshare guide:
   https://docs.waveshare.net/ESP32-S3-Touch-LCD-7/ESP-IDF

2. Clone official examples for reference:
   ```bash
   git clone https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7.git
   ```
   ESP-IDF examples are under `examples/ESP-IDF/`.
   Start with `08_lvgl_v8_demo` or `09_lvgl_v9_demo` for display.

3. For this starter project:
   ```bash
   cd starter
   idf.py set-target esp32s3
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor   # adjust port
   ```

## Starter Project

The `starter/` directory contains a minimal ESP-IDF project skeleton configured for ESP32-S3 with PSRAM. Extend it with Waveshare LCD drivers or Grok Gadgets style commands.

## Notes

- First flash may require holding BOOT while connecting USB, then release and flash.
- Display uses RGB parallel bus + CH422G IO expander for backlight/touch reset.
- For Grok Gadgets integration: adapt communication layer; display needs custom LVGL or RGB driver commands.

## Resources

- Waveshare Docs: https://docs.waveshare.net/ESP32-S3-Touch-LCD-7/
- Waveshare Examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7
- ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/
