"""Idempotent patches for espressif/esp_lvgl_adapter (RGB freeze on ESP32-S3).

1) Bridge calloc -> INTERNAL DRAM so esp_lcd_rgb_panel_register_event_callbacks
   accepts user_ctx (PSRAM user_ctx => ESP_ERR_INVALID_ARG / frozen UI).
2) TRIPLE/DOUBLE_PARTIAL draw buffer respects profile.use_psram (was hardcoded
   false => 80KB INTERNAL alloc fail when WiFi+RGB bounce already fragment DRAM).
"""
from __future__ import annotations

import pathlib
import sys

MARKER_BRIDGE = "/* grokbot: bridge user_ctx in INTERNAL DRAM */"
MARKER_PARTIAL = "/* grokbot: partial draw buf honor use_psram */"

OLD_CALLOC = "esp_lv_adapter_display_bridge_v9_t *impl = calloc(1, sizeof(*impl));"
NEW_CALLOC = (
    MARKER_BRIDGE + "\n"
    "    esp_lv_adapter_display_bridge_v9_t *impl = heap_caps_calloc(\n"
    "        1, sizeof(*impl), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);"
)

OLD_PARTIAL = (
    "void *buf = display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, false);"
)
NEW_PARTIAL = (
    MARKER_PARTIAL + "\n"
    "        void *buf = display_manager_alloc_draw_buffer(\n"
    "            cfg->draw_buf_pixels * color_size, profile->use_psram);"
)


def patch_file(path: pathlib.Path, old: str, new: str, marker: str) -> str:
    text = path.read_text(encoding="utf-8")
    if marker in text:
        return "already"
    if old not in text:
        return "missing"
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
    return "patched"


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    bridge = root / "src/display/bridge/v9/lvgl_bridge_v9.c"
    dm = root / "src/display/display_manager.c"
    if not bridge.is_file() or not dm.is_file():
        print(f"adapter not found under {root}")
        return 0

    r1 = patch_file(bridge, OLD_CALLOC, NEW_CALLOC, MARKER_BRIDGE)
    r2 = patch_file(dm, OLD_PARTIAL, NEW_PARTIAL, MARKER_PARTIAL)
    print(f"bridge_calloc={r1} partial_psram={r2}")
    if "missing" in (r1, r2):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
