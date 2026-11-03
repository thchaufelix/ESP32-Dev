#!/usr/bin/env python3
"""Slice Fubuki idle sprite sheet into RGB565 LE frames for LVGL."""

from __future__ import annotations

import pathlib
import struct

from PIL import Image

# Original layout: 6 frames left-to-right, each 1008x1792 (sheet 6048x1792).
FRAME_COUNT = 6
SRC_FW_ORIG = 1008
SRC_FH_ORIG = 1792
# Fit 70% of 800x480 (560x480) without stretching the portrait.
OUT_H = 480
OUT_W = int(round(OUT_H * SRC_FW_ORIG / SRC_FH_ORIG))  # 270

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "assets" / "sprite_sheet" / "fubuki_idle_extend.png"
OUT_DIR = ROOT / "main" / "assets" / "idle_fubuki"


def rgb565_le(r: int, g: int, b: int) -> bytes:
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack("<H", v)


def main() -> None:
    im = Image.open(SRC).convert("RGB")
    sheet_w, sheet_h = im.size
    out_w = OUT_W
    print(f"sheet {sheet_w}x{sheet_h} -> {FRAME_COUNT} frames -> {out_w}x{OUT_H}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for i in range(FRAME_COUNT):
        x0 = int(round(i * sheet_w / FRAME_COUNT))
        x1 = int(round((i + 1) * sheet_w / FRAME_COUNT))
        frame = im.crop((x0, 0, x1, sheet_h)).resize((out_w, OUT_H), Image.Resampling.LANCZOS)
        px = frame.tobytes()
        raw = bytearray(out_w * OUT_H * 2)
        o = 0
        for p in range(0, len(px), 3):
            raw[o : o + 2] = rgb565_le(px[p], px[p + 1], px[p + 2])
            o += 2
        out = OUT_DIR / f"fubuki_idle_{out_w}x{OUT_H}_{i}.bin"
        out.write_bytes(raw)
        print(f"wrote {out.name} crop={x1 - x0}x{sheet_h} {len(raw)} bytes")


if __name__ == "__main__":
    main()
