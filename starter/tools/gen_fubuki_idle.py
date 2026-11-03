#!/usr/bin/env python3
"""Slice Fubuki idle sprite sheet into RGB565 LE frames for LVGL."""

from __future__ import annotations

import pathlib
import struct

from PIL import Image

FRAME_COUNT = 4
# Fit 70% of 800x480 (560x480) without stretching the portrait.
OUT_H = 480

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "assets" / "sprite_sheet" / "fubuki_idle.png"
OUT_DIR = ROOT / "main" / "assets" / "idle_fubuki"


def rgb565_le(r: int, g: int, b: int) -> bytes:
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack("<H", v)


def main() -> None:
    im = Image.open(SRC).convert("RGB")
    sheet_w, sheet_h = im.size
    if sheet_w % FRAME_COUNT != 0:
        raise SystemExit(f"sheet width {sheet_w} is not divisible by {FRAME_COUNT}")
    src_fw = sheet_w // FRAME_COUNT
    src_fh = sheet_h
    out_w = int(round(OUT_H * src_fw / src_fh))
    print(f"sheet {sheet_w}x{sheet_h} -> {FRAME_COUNT}x {src_fw}x{src_fh} -> {out_w}x{OUT_H}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for i in range(FRAME_COUNT):
        box = (i * src_fw, 0, (i + 1) * src_fw, src_fh)
        frame = im.crop(box).resize((out_w, OUT_H), Image.Resampling.LANCZOS)
        px = frame.tobytes()
        raw = bytearray(out_w * OUT_H * 2)
        o = 0
        for p in range(0, len(px), 3):
            raw[o : o + 2] = rgb565_le(px[p], px[p + 1], px[p + 2])
            o += 2
        out = OUT_DIR / f"fubuki_idle_{out_w}x{OUT_H}_{i}.bin"
        out.write_bytes(raw)
        print(f"wrote {out.name} {out_w}x{OUT_H} {len(raw)} bytes")


if __name__ == "__main__":
    main()
