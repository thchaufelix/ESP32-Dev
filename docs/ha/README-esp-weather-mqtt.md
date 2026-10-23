# ESP32 weather via Home Assistant MQTT

## Topics
| Topic | Direction | Notes |
|-------|-----------|--------|
| `ha/weather/home` | HA → ESP | Retained JSON, QoS1 |
| `ha/weather/home/refresh` | ESP → HA | Payload `1` asks HA to republish |

## ESP Settings
1. Flash `feature/ha-mqtt-weather`
2. Open **Settings** (gear)
3. Set **Broker IP / host** to Synology NAS LAN IP (or Tailscale IP)
4. Port default **1883**
5. Save → MQTT reconnects if IP changed
6. Tap place name on weather panel to request refresh

## HA setup
1. MQTT integration → broker `127.0.0.1:1883` (host network Mosquitto)
2. Install package `esp_weather_mqtt.yaml` (see file comments)
3. Confirm entity `weather.home` (met.no). Rename in YAML if different.
4. After restart, check Mosquitto / MQTT explorer for retained `ha/weather/home`

## Merge note
Parallel open branches (merge after this, or rebase):
- `feature/lrt-one-platform-cycle`
- `feature/wifi-keyboard-dismiss-outside`
Branch from Development tip `0d73b3c`.
