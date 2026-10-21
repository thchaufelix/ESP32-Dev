# Display / Flash / PSRAM notes (ESP32-S3-Touch-LCD-7)

## White screen after Flash+PSRAM 120 MHz + HPM (2026-10-21)

**Symptom:** After flashing a build with matched Flash+PSRAM **120 MHz** and
`SPI_FLASH_HPM_ENA` / `BOOTLOADER_FLASH_DC_AWARE`, the panel was **all white**.
CH422G backlight stayed on; RGB framebuffer / PSRAM XIP was unusable.

**Also seen:** Live `sdkconfig` could disagree with defaults
(`FLASHFREQ_120M=y` but `FLASHFREQ="80m"` string mismatch).

**Working recovery (commit `c82f1fc`, later reverted for a 120M fullclean retest):**

- Matched **Flash + PSRAM 80 MHz** (same as Waveshare `09_lvgl_v9_demo`)
- Do **not** enable `CONFIG_SPI_FLASH_HPM_ENA` / `CONFIG_BOOTLOADER_FLASH_DC_AWARE` for this board path
- **PCLK 16 MHz**, bounce buffer **~10 lines**
- Keep HTTPS hardening (mbedtls buffers in PSRAM, soft heap gate, AES soft fallback, no 1 Hz `uxTaskGetSystemState` CPU% sampling)

**sdkconfig.defaults keys for the 80M recovery:**

```
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
# CONFIG_ESPTOOLPY_FLASHFREQ_120M is not set
# CONFIG_BOOTLOADER_FLASH_DC_AWARE is not set
# CONFIG_SPI_FLASH_HPM_ENA is not set
CONFIG_SPIRAM_SPEED_80M=y
# CONFIG_SPIRAM_SPEED_120M is not set
```

**PCLK** in `starter/components/waveshare_rgb_lcd_port.h`:

```
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ      (16 * 1000 * 1000)
#define EXAMPLE_RGB_BOUNCE_LINES        (10)
```

**120 MHz path that was under retest** (Waveshare instructions / RGB bandwidth):
`FLASHFREQ_120M` + `SPIRAM_SPEED_120M` + HPM + DC_AWARE. That path previously
built only after silencing HPM `#warning`s (`25e32a6`) but produced the white
screen in device testing. Prefer **fullclean** whenever changing these clocks.

**Related:** Espressif RGB drift FAQ; Waveshare ESP32-S3-Touch-LCD-7 menuconfig
list. Drift ≠ white screen: 1s rhythm drift was improved by stopping per-second
CPU% sampling; white screen was specifically 120M+HPM.

## 2026-10-21 UI freeze after 80M restore (edbe1cc)
Symptom: panel shows first frame, then clock/weather/LRT never update.
Cause: PCLK 16 MHz + Flash/PSRAM 80 MHz made RGB bounce ISR dominate CPU;
LVGL task starved (FB kept scanning = frozen picture). Also clock timer
was 15s and WiFi PS_MIN_MODEM slowed NTP/first HTTPS.
Fix: PCLK 12 MHz, clock/poll timers 1s (still no uxTaskGetSystemState), WIFI_PS_NONE.
Keep matched 80 MHz flash+PSRAM (no 120M+HPM).
