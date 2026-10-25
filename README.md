# ESP32-S3-Touch-LCD-7 Development Project

ESP-IDF app for the [Waveshare ESP32-S3-Touch-LCD-7](https://docs.waveshare.net/ESP32-S3-Touch-LCD-7/): Home Assistant weather over MQTT, Hong Kong Light Rail (LRT) next-train timetable, WiFi/settings UI, and idle power saving.

Track **`Development`** for day-to-day work (not `main` unless you intend a release cut).

## Hardware

- Board: Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3N16R8)
- 7" 800×480 RGB IPS capacitive touch LCD (GT911)
- 16 MB Flash + 8 MB PSRAM
- Interfaces: USB, I2C, RS485, CAN, SD card, battery, etc.

## Features (current)

### Home screen

- **Layout:** left **LRT timetable ~55%** / right **weather ~45%**; clock + status in the top bar.
- **Weather:** pushed from Home Assistant over MQTT (retained JSON). Tap the place name to request a HA refresh. Cadence is owned by HA (typically ~15 min), not by a local poll timer.
- **LRT:** polls MTR Next Train API on a Settings interval (default **1 min**) inside a daily period window. One platform shown at a time; tap the platform header to cycle. Tap a **route number** to highlight that row (persists across refreshes until cleared or the station changes).
- **Station picker:** open from Settings (or the schedule station control). Stations grouped by district (屯門 / 元朗 / 天水圍), collapsible sections, up to **3 pinned** stations at the top. Default stop: 藍地 (`350`).

### Settings (gear)

- **MQTT broker (HA weather):** Broker IP / host, port (default **1883**). Save reconnects MQTT if host/port changed.
- **Timetable (LRT):** Interval (min), Period start/end (local Asia/Shanghai hours), station picker entry.
- **Display → Idle dim:** minutes with no touch before idle (default **10**, range **0–180**; **0** = never). Persisted in NVS.
- Separate **WiFi Settings** page for SSID/password (NVS). Optional compile-time fallback: `wifi_config.h` (gitignored).

### Idle dim / wake

After **Idle dim** minutes with no touch:

1. Turn CH422G **backlight off**
2. **Stop RGB panel** (`panel_del`) so PCLK / DMA / bounce stop
3. **Pause LRT API** fetches (WiFi + MQTT stay connected)

**Wake** on touch: recreate RGB panel, recover LVGL adapter, backlight on, resume LRT + **immediate** fetch. The **wake gesture is ignored as UI input** (no accidental taps on controls underneath).

Rough power (USB 5 V, not measured): wake ~400–500 mA; idle (backlight + RGB off, WiFi/MQTT on) ~80–150 mA mid ~100 mA.

## Prerequisites

- ESP-IDF v5.1+ (Waveshare LVGL v9 demo targets ~5.2/5.3; Component Manager required)
- VS Code with ESP-IDF extension (recommended), or ESP-IDF export script in a terminal
- USB Type-C **data** cable

## Quick start

1. Optional WiFi fallback (prefer on-screen WiFi Settings → NVS):

```bash
cd starter/main
copy wifi_config.h.example wifi_config.h
# edit WIFI_SSID / WIFI_PASSWORD — never commit wifi_config.h
```

2. Build and flash (ESP-IDF environment / VS Code ESP-IDF terminal):

```bash
cd starter
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

First build downloads LVGL / `esp_lvgl_adapter` / GT911 into `managed_components/` (gitignored). CMake re-applies `tools/patch_esp_lvgl_adapter.py` after component refresh (RGB bridge INTERNAL alloc).

3. After boot: connect WiFi, open **Settings**, set MQTT broker host, Save. Confirm HA package is publishing (see below).

### Flash notes

- First flash may need **BOOT** held while connecting USB, then release and flash.
- Prefer **matched Flash + PSRAM 80 MHz** (see `starter/sdkconfig.defaults`). Avoid 120 MHz + HPM on this board path — can white-screen.
- After changing flash/PSRAM clocks or tear mode: `idf.py fullclean` then rebuild.
- Tear mode is **DOUBLE_FULL** (panel framebuffers in PSRAM). Longer display / DRAM notes: `starter/NOTES_display_flash.md`.

## Home Assistant weather (MQTT)

| Topic | Direction | Notes |
|-------|-----------|--------|
| `ha/weather/home` | HA → ESP | Retained JSON, QoS1 |
| `ha/weather/home/refresh` | ESP → HA | Payload `1` asks HA to republish |

Docs and package:

- `docs/ha/README-esp-weather-mqtt.md`
- `docs/ha/esp_weather_mqtt.yaml`

## What's in `starter/`

| Path | Role |
|------|------|
| `main/main.c` | Boot: NVS, RGB+touch, idle bind, WiFi, UI |
| `main/app/ui/` | Home split, LRT schedule/picker, weather, settings, idle, topbar/clock, WiFi UI |
| `main/app/lrt/` | Next Train client + station table (districts) |
| `main/app/mqtt/` | HA MQTT client (`ha/weather/home`) |
| `main/app/weather/` | Weather model / parse for UI |
| `main/app/settings/` | NVS settings (MQTT, LRT schedule/pins, idle) |
| `main/app/wifi/` | STA manager |
| `components/waveshare_rgb_lcd_port.*` | RGB LCD + CH422G backlight + GT911 |
| `tools/patch_esp_lvgl_adapter.py` | Re-applied adapter INTERNAL/PSRAM fixes |
| `main/idf_component.yml` | `lvgl ^9`, `esp_lvgl_adapter`, `esp_lcd_touch_gt911` |

## Resources

- Waveshare Docs: https://docs.waveshare.net/ESP32-S3-Touch-LCD-7/
- Waveshare Examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7
- ESP-IDF (ESP32-S3): https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/