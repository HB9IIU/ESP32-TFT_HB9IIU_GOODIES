#!/usr/bin/env python3
"""
Generate RGB565 C headers from local random image files.

Behavior:
- Scans this folder for image files: .jpg/.jpeg/.png/.webp/.bmp
- Generates one header per image into ../../src
- Output names follow the source stem only: <source_stem>.h
- Detects source image format
- Applies smart crop to target size before RGB565 conversion
"""

from pathlib import Path
from PIL import Image

INPUT_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp", ".bmp"}
OUTPUT_DIR = Path("../../src")
VALUES_PER_LINE = 12
# 4"
TARGET_WIDTH = 480
TARGET_HEIGHT = 320
# 2.4......
#TARGET_WIDTH = 320
#TARGET_HEIGHT = 240

def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def make_c_identifier(stem: str) -> str:
    out = "".join(ch if ch.isalnum() else "_" for ch in stem)
    while "__" in out:
        out = out.replace("__", "_")
    out = out.strip("_")
    if not out:
        out = "image"
    if out[0].isdigit():
        out = "img_" + out
    return out


def smart_crop_to_target(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    src_w, src_h = img.size
    target_aspect = target_w / target_h
    src_aspect = src_w / src_h

    if src_aspect > target_aspect:
        crop_h = src_h
        crop_w = int(round(crop_h * target_aspect))
        gray = img.convert("L")
        # Saliency from horizontal intensity changes; choose the most "informative" window.
        row_center = src_h // 2
        energies = []
        for x in range(0, src_w - crop_w + 1):
            e = 0
            for y in range(max(1, row_center - 20), min(src_h - 1, row_center + 20)):
                prev_px = gray.getpixel((x, y))
                for dx in range(1, min(crop_w, 64)):
                    cur_px = gray.getpixel((x + dx, y))
                    e += abs(cur_px - prev_px)
                    prev_px = cur_px
            energies.append(e)
        if energies:
            x0 = max(0, min(src_w - crop_w, energies.index(max(energies))))
        else:
            x0 = (src_w - crop_w) // 2
        y0 = 0
    else:
        crop_w = src_w
        crop_h = int(round(crop_w / target_aspect))
        gray = img.convert("L")
        # Saliency from vertical intensity changes; choose the most "informative" window.
        col_center = src_w // 2
        energies = []
        for y in range(0, src_h - crop_h + 1):
            e = 0
            for x in range(max(1, col_center - 20), min(src_w - 1, col_center + 20)):
                prev_px = gray.getpixel((x, y))
                for dy in range(1, min(crop_h, 64)):
                    cur_px = gray.getpixel((x, y + dy))
                    e += abs(cur_px - prev_px)
                    prev_px = cur_px
            energies.append(e)
        if energies:
            y0 = max(0, min(src_h - crop_h, energies.index(max(energies))))
        else:
            y0 = (src_h - crop_h) // 2
        x0 = 0

    cropped = img.crop((x0, y0, x0 + crop_w, y0 + crop_h))
    return cropped.resize((target_w, target_h), Image.Resampling.LANCZOS)


def build_header_text(
    input_image: Path,
    output_stem: str,
    source_format: str,
    width: int,
    height: int,
    values: list[int],
) -> str:
    array_name = make_c_identifier(output_stem)
    lines = []
    lines.append("// Universal RGB565 image header")
    lines.append("#pragma once")
    lines.append("#include <Arduino.h>")
    lines.append("")
    lines.append("// Universal struct for RGB565 images")
    lines.append("#ifndef RGB565_IMAGE_STRUCT_DEFINED")
    lines.append("#define RGB565_IMAGE_STRUCT_DEFINED")
    lines.append("struct RGB565Image {")
    lines.append("    uint16_t width;")
    lines.append("    uint16_t height;")
    lines.append("    const uint16_t* data;")
    lines.append("};")
    lines.append("#endif")
    lines.append("")
    lines.append(f"// Generated from {input_image.name} [{source_format}] ({width}x{height}) -> RGB565")
    lines.append(f"const uint16_t {array_name}[{width}*{height}] PROGMEM = {{")

    for i in range(0, len(values), VALUES_PER_LINE):
        chunk = values[i : i + VALUES_PER_LINE]
        lines.append("  " + ", ".join(f"0x{v:04X}" for v in chunk) + ",")

    lines.append("};")
    lines.append("")
    lines.append("// Provide an instance for universal display")
    lines.append(f"const RGB565Image {array_name}_img = {{")
    lines.append(f"  {width}, // width")
    lines.append(f"  {height}, // height")
    lines.append(f"  {array_name}")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def convert_one_image(input_image: Path, output_dir: Path, output_stem: str) -> Path:
    src = Image.open(input_image)
    source_format = (src.format or input_image.suffix.lstrip(".")).upper()

    # Normalize alpha images onto black background before crop/convert.
    if src.mode in ("RGBA", "LA") or (src.mode == "P" and "transparency" in src.info):
        rgba = src.convert("RGBA")
        base = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
        src = Image.alpha_composite(base, rgba).convert("RGB")
    else:
        src = src.convert("RGB")

    img = smart_crop_to_target(src, TARGET_WIDTH, TARGET_HEIGHT)
    width, height = img.size
    output_header = output_dir / f"{output_stem}.h"

    raw = img.tobytes()
    values = [
        rgb888_to_rgb565(raw[i], raw[i + 1], raw[i + 2])
        for i in range(0, len(raw), 3)
    ]
    header_text = build_header_text(input_image, output_stem, source_format, width, height, values)
    output_header.write_text(header_text, encoding="utf-8")
    return output_header


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    output_dir = (script_dir / OUTPUT_DIR).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    candidates = sorted(
        p for p in script_dir.iterdir()
        if p.is_file() and p.suffix.lower() in INPUT_SUFFIXES
    )
    if not candidates:
        exts = ", ".join(sorted(INPUT_SUFFIXES))
        raise SystemExit(f"No input images found with extensions: {exts} in {script_dir}")

    # Keep one source per base stem (newest wins), so random_image0 does not
    # generate both *_jpg.h and *_png.h from stale files.
    by_stem: dict[str, Path] = {}
    for p in candidates:
        prev = by_stem.get(p.stem)
        if prev is None or p.stat().st_mtime >= prev.stat().st_mtime:
            by_stem[p.stem] = p
    input_images = [by_stem[s] for s in sorted(by_stem.keys())]

    for input_image in input_images:
        # Remove stale generated variants for the same source stem.
        for old in output_dir.glob(f"{input_image.stem}_*.h"):
            old.unlink()
        output_stem = input_image.stem
        out = convert_one_image(input_image, output_dir, output_stem)
        print(f"Wrote: {out}")


if __name__ == "__main__":
    main()
