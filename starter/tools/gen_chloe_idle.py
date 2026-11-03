#!/usr/bin/env python3
"""Slice Sakamata Chloe idle sprite sheet into RGB565 LE frames for LVGL."""

from __future__ import annotations

import pathlib
import struct

from PIL import Image

# Original layout: 6 frames left-to-right, each 720x1280 (sheet 4320x1280).
FRAME_COUNT = 6
SRC_FW_ORIG = 720
SRC_FH_ORIG = 1280
# Fit 70% of 800x480 (560x480). Keep source aspect; pad with BG.
OUT_H = 480
OUT_W = int(round(OUT_H * SRC_FW_ORIG / SRC_FH_ORIG))  # 270
BG = (0x14, 0x12, 0x13)

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "assets" / "sprite_sheet" / "Sakamata_Chloe_idle_extend.png"
OUT_DIR = ROOT / "main" / "assets" / "idle_chloe"


def rgb565_le(r: int, g: int, b: int) -> bytes:
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack("<H", v)


def fit_keep_aspect(src: Image.Image, box_w: int, box_h: int, bg: tuple[int, int, int]) -> Image.Image:
    sw, sh = src.size
    scale = min(box_w / sw, box_h / sh)
    dw = max(1, int(round(sw * scale)))
    dh = max(1, int(round(sh * scale)))
    fitted = src.resize((dw, dh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (box_w, box_h), bg)
    canvas.paste(fitted, ((box_w - dw) // 2, (box_h - dh) // 2))
    return canvas


def main() -> None:
    im = Image.open(SRC).convert("RGB")
    sheet_w, sheet_h = im.size
    print(f"sheet {sheet_w}x{sheet_h} -> {FRAME_COUNT} frames -> {OUT_W}x{OUT_H} pad=#{BG[0]:02x}{BG[1]:02x}{BG[2]:02x}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for i in range(FRAME_COUNT):
        x0 = int(round(i * sheet_w / FRAME_COUNT))
        x1 = int(round((i + 1) * sheet_w / FRAME_COUNT))
        frame = fit_keep_aspect(im.crop((x0, 0, x1, sheet_h)), OUT_W, OUT_H, BG)
        px = frame.tobytes()
        raw = bytearray(OUT_W * OUT_H * 2)
        o = 0
        for p in range(0, len(px), 3):
            raw[o : o + 2] = rgb565_le(px[p], px[p + 1], px[p + 2])
            o += 2
        out = OUT_DIR / f"chloe_idle_{OUT_W}x{OUT_H}_{i}.bin"
        out.write_bytes(raw)
        print(f"wrote {out.name} crop={x1 - x0}x{sheet_h} {len(raw)} bytes")


if __name__ == "__main__":
    main()
