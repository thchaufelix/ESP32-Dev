#!/usr/bin/env python3
"""Convert weather yukata JPGs to RGB565 LE, keeping portrait aspect."""

from __future__ import annotations

import pathlib
import struct

from PIL import Image

OUT_H = 302
OUT_W = int(round(OUT_H * 720 / 1280))  # 170
BG = (0x14, 0x14, 0x14)

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "assets" / "yukata"
JOBS = (
    ("plain_dark_Fubuki_yukata_P010_front_style1.jpg", ROOT / "main" / "assets" / "fubuki" / "fubuki_yukata_170x302.bin"),
    ("plain_dark_Aqua_yukata_P010_front_style1.jpg", ROOT / "main" / "assets" / "aqua" / "aqua_yukata_170x302.bin"),
)


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


def write_rgb565(im: Image.Image, dest: pathlib.Path) -> None:
    px = im.tobytes()
    raw = bytearray(im.width * im.height * 2)
    o = 0
    for p in range(0, len(px), 3):
        raw[o : o + 2] = rgb565_le(px[p], px[p + 1], px[p + 2])
        o += 2
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(raw)
    print(f"wrote {dest.relative_to(ROOT)} {im.width}x{im.height} {len(raw)} bytes")


def main() -> None:
    for name, dest in JOBS:
        src = SRC_DIR / name
        im = Image.open(src).convert("RGB")
        frame = fit_keep_aspect(im, OUT_W, OUT_H, BG)
        write_rgb565(frame, dest)


if __name__ == "__main__":
    main()
