#!/usr/bin/env python3
"""Convert weather rough-doodle JPGs: key white paper to panel #0e0e18, RGB565 LE."""

from __future__ import annotations

import hashlib
import pathlib
import shutil
import struct
from collections import deque

from PIL import Image

# Weather / panel background.
BG = (0x0E, 0x0E, 0x18)
# Per-channel distance from 255. Flood from edges only so Fubuki white hair/kimono stays.
WHITE_THR = 20
OUT_H = 240
CROP_PAD = 8

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC_ROOT = pathlib.Path(r"D:\GrokProject\image_generate\custom_style\15_rough_doodle\samples")
ASSET_DIR = ROOT / "assets" / "rough_doodle"
JOBS = (
    {
        "src_name": "fubuki_rough_doodle_style15.jpg",
        "stem": "fubuki_doodle",
        "out_dir": ROOT / "main" / "assets" / "fubuki",
        "guard": "FUBUKI_DOODLE_H",
        "w_macro": "FUBUKI_DOODLE_W",
        "h_macro": "FUBUKI_DOODLE_H",
        "dsc": "fubuki_doodle",
        "header": "fubuki_doodle.h",
    },
    {
        "src_name": "mio_rough_doodle_style15.jpg",
        "stem": "mio_doodle",
        "out_dir": ROOT / "main" / "assets" / "mio",
        "guard": "MIO_DOODLE_H",
        "w_macro": "MIO_DOODLE_W",
        "h_macro": "MIO_DOODLE_H",
        "dsc": "mio_doodle",
        "header": "mio_doodle.h",
    },
)


def rgb565_le(r: int, g: int, b: int) -> bytes:
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack("<H", v)


def is_near_white(rgb: tuple[int, int, int], thr: int) -> bool:
    r, g, b = rgb
    return (255 - r) <= thr and (255 - g) <= thr and (255 - b) <= thr


def flood_key_white(im: Image.Image, bg: tuple[int, int, int], thr: int) -> Image.Image:
    """Replace edge-connected near-white paper with bg. Interior whites stay."""
    im = im.convert("RGB")
    w, h = im.size
    pix = im.load()
    vis = bytearray(w * h)
    q: deque[tuple[int, int]] = deque()

    def push(x: int, y: int) -> None:
        if x < 0 or y < 0 or x >= w or y >= h:
            return
        i = y * w + x
        if vis[i]:
            return
        if not is_near_white(pix[x, y], thr):
            return
        vis[i] = 1
        q.append((x, y))

    for x in range(w):
        push(x, 0)
        push(x, h - 1)
    for y in range(h):
        push(0, y)
        push(w - 1, y)
    while q:
        x, y = q.popleft()
        pix[x, y] = bg
        push(x + 1, y)
        push(x - 1, y)
        push(x, y + 1)
        push(x, y - 1)
    return im


def content_bbox(im: Image.Image, bg: tuple[int, int, int], pad: int) -> tuple[int, int, int, int]:
    w, h = im.size
    pix = im.load()
    min_x, min_y, max_x, max_y = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            if pix[x, y] != bg:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y
    if max_x < 0:
        return (0, 0, w, h)
    min_x = max(0, min_x - pad)
    min_y = max(0, min_y - pad)
    max_x = min(w - 1, max_x + pad)
    max_y = min(h - 1, max_y + pad)
    return (min_x, min_y, max_x + 1, max_y + 1)


def fit_keep_aspect(src: Image.Image, box_w: int, box_h: int, bg: tuple[int, int, int]) -> Image.Image:
    sw, sh = src.size
    scale = min(box_w / sw, box_h / sh)
    dw = max(1, int(round(sw * scale)))
    dh = max(1, int(round(sh * scale)))
    fitted = src.resize((dw, dh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (box_w, box_h), bg)
    canvas.paste(fitted, ((box_w - dw) // 2, (box_h - dh) // 2))
    return canvas


def write_rgb565(im: Image.Image, dest: pathlib.Path) -> bytes:
    px = im.tobytes()
    raw = bytearray(im.width * im.height * 2)
    o = 0
    for p in range(0, len(px), 3):
        raw[o : o + 2] = rgb565_le(px[p], px[p + 1], px[p + 2])
        o += 2
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(raw)
    return bytes(raw)


def write_lvgl_wrapper(job: dict, out_w: int, out_h: int, bin_name: str) -> None:
    out_dir: pathlib.Path = job["out_dir"]
    out_dir.mkdir(parents=True, exist_ok=True)
    header = out_dir / job["header"]
    c_file = out_dir / (job["stem"] + ".c")
    dsc = job["dsc"]
    bin_sym = bin_name.replace(".", "_") + "_start"
    header.write_text(
        "#pragma once\n"
        "\n"
        "#include \"lvgl.h\"\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n"
        f"#define {job['w_macro']} {out_w}\n"
        f"#define {job['h_macro']} {out_h}\n"
        "\n"
        f"extern const lv_image_dsc_t {dsc}_{out_w}x{out_h};\n"
        "\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n",
        encoding="utf-8",
        newline="\n",
    )
    c_file.write_text(
        f"#include \"{job['header']}\"\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        f"extern const uint8_t {bin_sym}[] asm(\"_binary_{bin_sym}\");\n"
        "\n"
        f"#define FRAME_BYTES ({job['w_macro']} * {job['h_macro']} * 2)\n"
        "\n"
        f"const lv_image_dsc_t {dsc}_{out_w}x{out_h} = {{\n"
        "    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
        "    .header.cf = LV_COLOR_FORMAT_RGB565,\n"
        "    .header.flags = 0,\n"
        f"    .header.w = {job['w_macro']},\n"
        f"    .header.h = {job['h_macro']},\n"
        f"    .header.stride = {job['w_macro']} * 2,\n"
        "    .data_size = FRAME_BYTES,\n"
        f"    .data = {bin_sym},\n"
        "};\n",
        encoding="utf-8",
        newline="\n",
    )


def copy_sources() -> None:
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    for job in JOBS:
        dest = ASSET_DIR / job["src_name"]
        src = SRC_ROOT / job["src_name"]
        if src.is_file():
            shutil.copy2(src, dest)
            print(f"copied {src} -> {dest.relative_to(ROOT)}")
        elif not dest.is_file():
            raise SystemExit(f"missing source {src} and {dest}")


def main() -> None:
    copy_sources()
    keyed_crops: list[Image.Image] = []
    for job in JOBS:
        src = ASSET_DIR / job["src_name"]
        im = Image.open(src).convert("RGB")
        keyed = flood_key_white(im, BG, WHITE_THR)
        box = content_bbox(keyed, BG, CROP_PAD)
        crop = keyed.crop(box)
        keyed_crops.append(crop)
        print(f"{job['src_name']} {im.size} -> keyed crop {crop.size} box={box}")

    max_ratio = max(im.width / im.height for im in keyed_crops)
    out_w = max(1, int(round(OUT_H * max_ratio)))
    out_h = OUT_H
    print(f"shared canvas {out_w}x{out_h} pad=#{BG[0]:02x}{BG[1]:02x}{BG[2]:02x} thr={WHITE_THR}")

    for job, crop in zip(JOBS, keyed_crops):
        frame = fit_keep_aspect(crop, out_w, out_h, BG)
        bin_name = f"{job['stem']}_{out_w}x{out_h}.bin"
        dest = job["out_dir"] / bin_name
        raw = write_rgb565(frame, dest)
        sha = hashlib.sha256(raw).hexdigest()
        write_lvgl_wrapper(job, out_w, out_h, bin_name)
        print(f"wrote {dest.relative_to(ROOT)} {frame.width}x{frame.height} {len(raw)} bytes sha256={sha}")


if __name__ == "__main__":
    main()
