#!/usr/bin/env python3
"""Prepare generated RGBA artwork for LVGL 9 and emit compact ARGB8565 C assets."""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


ICON_SIZES = {
    "book": 64,
    "compass": 64,
    "english": 64,
    "cup": 56,
    "gear": 48,
    "star": 72,
    "weather_sunny": 60,
    "weather_partly_cloudy": 60,
    "weather_cloudy": 60,
    "weather_rain": 60,
    "weather_thunder": 60,
    "weather_snow": 60,
    "weather_fog": 60,
    "menu_report": 56,
    "menu_schedule": 56,
    "menu_alarm": 56,
    "menu_wifi": 56,
    "menu_home": 56,
}


def content_bbox(image: Image.Image, checkerboard: bool) -> tuple[int, int, int, int]:
    rgba = np.asarray(image.convert("RGBA"))
    if checkerboard:
        rgb = rgba[..., :3].astype(np.int16)
        mask = rgb.max(axis=2) - rgb.min(axis=2) > 18
    else:
        mask = rgba[..., 3] > 12
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return (0, 0, image.width, image.height)
    margin = max(4, int(max(xs[-1] - xs[0], ys[-1] - ys[0]) * 0.025))
    return (
        max(0, int(xs.min()) - margin),
        max(0, int(ys.min()) - margin),
        min(image.width, int(xs.max()) + margin + 1),
        min(image.height, int(ys.max()) + margin + 1),
    )


def clear_checkerboard(image: Image.Image) -> Image.Image:
    """Flood only neutral pixels connected to an edge, retaining white inset details."""
    rgba = np.asarray(image.convert("RGBA")).copy()
    rgb = rgba[..., :3].astype(np.int16)
    neutral = (rgb.max(axis=2) - rgb.min(axis=2)) <= 22
    height, width = neutral.shape
    background = np.zeros((height, width), dtype=bool)
    queue: deque[tuple[int, int]] = deque()

    for x in range(width):
        if neutral[0, x]:
            queue.append((x, 0))
        if neutral[height - 1, x]:
            queue.append((x, height - 1))
    for y in range(height):
        if neutral[y, 0]:
            queue.append((0, y))
        if neutral[y, width - 1]:
            queue.append((width - 1, y))

    while queue:
        x, y = queue.popleft()
        if background[y, x] or not neutral[y, x]:
            continue
        background[y, x] = True
        if x:
            queue.append((x - 1, y))
        if x + 1 < width:
            queue.append((x + 1, y))
        if y:
            queue.append((x, y - 1))
        if y + 1 < height:
            queue.append((x, y + 1))

    alpha = Image.fromarray((~background).astype(np.uint8) * 255, "L")
    alpha = alpha.filter(ImageFilter.GaussianBlur(0.55))
    rgba[..., 3] = np.asarray(alpha)
    rgba[rgba[..., 3] == 0, :3] = 0
    return Image.fromarray(rgba, "RGBA")


def prepare(source: Path, size: int, checkerboard: bool) -> Image.Image:
    image = Image.open(source).convert("RGBA")
    if image.getchannel("A").getextrema()[0] < 250:
        checkerboard = False
    image = image.crop(content_bbox(image, checkerboard))
    usable = size - 4
    image.thumbnail((usable, usable), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(image, ((size - image.width) // 2, (size - image.height) // 2))
    if checkerboard:
        canvas = clear_checkerboard(canvas)
    return canvas


def write_rgb565a8(name: str, image: Image.Image, destination: Path) -> None:
    pixels = np.asarray(image.convert("RGBA"), dtype=np.uint8)
    height, width = pixels.shape[:2]
    color_plane = bytearray()
    alpha_plane = bytearray()
    for red, green, blue, alpha in pixels.reshape((-1, 4)):
        rgb565 = ((int(red) >> 3) << 11) | ((int(green) >> 2) << 5) | (int(blue) >> 3)
        color_plane.extend((rgb565 & 0xFF, rgb565 >> 8))
        alpha_plane.append(int(alpha))
    packed = color_plane + alpha_plane

    lines = []
    for offset in range(0, len(packed), 18):
        lines.append("    " + "".join(f"0x{value:02x}," for value in packed[offset:offset + 18]))
    destination.write_text(
        "#include \"ui_icons.h\"\n\n"
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n\n"
        f"static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {name}_map[] = {{\n"
        + "\n".join(lines)
        + "\n};\n\n"
        + f"const lv_image_dsc_t {name} = {{\n"
        + "    .header = {\n"
        + "        .magic = LV_IMAGE_HEADER_MAGIC,\n"
        + "        .cf = LV_COLOR_FORMAT_RGB565A8,\n"
        + "        .flags = 0,\n"
        + f"        .w = {width},\n        .h = {height},\n        .stride = {width * 2},\n"
        + "    },\n"
        + f"    .data_size = sizeof({name}_map),\n    .data = {name}_map,\n"
        + "};\n",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    for key in ICON_SIZES:
        parser.add_argument(f"--{key.replace('_', '-')}", dest=key, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    for key, size in ICON_SIZES.items():
        source = getattr(args, key)
        if source is None:
            continue
        image = prepare(source, size, checkerboard=(key == "cup"))
        image.save(args.output / f"ui_icon_{key}.png", optimize=True)
        write_rgb565a8(f"ui_icon_{key}", image, args.output / f"ui_icon_{key}.c")


if __name__ == "__main__":
    main()
