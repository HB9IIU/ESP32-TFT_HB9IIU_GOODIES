#!/usr/bin/env python3
"""
fontMaker.py — FreeFont header generator for TFT_eSPI / Adafruit_GFX

Description:
    Converts a .ttf font file into TFT_eSPI-compatible FreeFont header files (.h) for use on ESP32 and similar devices.
    - Supports ASCII range 0x20..0x7E (space to ~) by default.
    - Packs bitmaps in the 1bpp bitstream format required by TFT_eSPI.

Installation:
    pip install freetype-py

Usage:
    python3 fontMaker.py --ttf ./MyFont.ttf --size 24 --out-dir ../../include

Output:
    <ttf-stem><size>pt7b.h   (default: next to the script)

Extras (index generation):
    After generating a folder of *pt7b.h headers, the script also writes two files used by the ESP32 project:
        - all_fonts.h   (includes every *pt7b.h in that folder)
        - fonts_index.h (defines kFonts[] and kFontCount for iterating fonts on ESP32)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import List, Tuple, Optional, Any


DEFAULT_SIZE_PT = 24
DEFAULT_FIRST = 0x20
DEFAULT_LAST = 0x7E
DEFAULT_DPI = 36

# Default “no-args” batch generation (what you asked for)
AUTO_MIN_SIZE_PT = 6
AUTO_MAX_SIZE_PT = 72
AUTO_STEP_PT = 1

# Default output directory (when --out-dir is not provided):
# Create a folder named after the font under include/Generated:Fonts_For_Copy.
# Example: include/Generated:Fonts_For_Copy/a/a24pt7b.h
AUTO_OUT_DIR_IS_FONT_SUBFOLDER = True
DEFAULT_OUT_ROOT_SUBDIR = "Generated:Fonts_For_Copy"

# Default location for source .ttf files (relative to this script).
DEFAULT_TTF_SUBDIR = "Add_New_ttf_fonts here"


def make_c_symbol(stem: str) -> str:
    s = "".join(ch if ch.isalnum() else "_" for ch in stem)
    while "__" in s:
        s = s.replace("__", "_")
    s = s.strip("_")
    if not s:
        s = "Font"
    if s[0].isdigit():
        s = "F_" + s
    return s


def hex_bytes(data: bytes, indent: str = "  ", cols: int = 12) -> str:
    parts = [f"0x{b:02X}" for b in data]
    lines = []
    for i in range(0, len(parts), cols):
        lines.append(indent + ", ".join(parts[i : i + cols]))
    return ",\n".join(lines)


def pack_bitmap_to_1bpp(ft_bitmap: Any) -> Tuple[bytes, int, int]:
    """
        Returns (packed_bytes, width, height) in the bitstream format used by
        Adafruit_GFX FreeFonts and TFT_eSPI FreeFonts:
        - pixels are traversed left-to-right, top-to-bottom
        - bits are packed MSB-first into bytes
        - IMPORTANT: there is NO per-row byte padding; rows can start mid-byte
            if width is not a multiple of 8
        FreeType's MONO bitmap storage is padded per row, so we always repack.
    """
    # Import lazily so scan/index mode works without freetype-py installed.
    import freetype  # type: ignore

    w = int(ft_bitmap.width)
    h = int(ft_bitmap.rows)
    pitch = int(ft_bitmap.pitch)
    buf = ft_bitmap.buffer

    pixel_mode = getattr(ft_bitmap, "pixel_mode", None)

    # Handle negative pitch (bottom-up storage)
    step = pitch
    if pitch < 0:
        pitch = -pitch

    def row_index(y: int) -> int:
        return (h - 1 - y) if step < 0 else y

    def mono_pixel_on(x: int, y: int) -> bool:
        ri = row_index(y)
        byte = buf[ri * pitch + (x >> 3)]
        mask = 0x80 >> (x & 7)
        return (byte & mask) != 0

    def gray_pixel_on(x: int, y: int) -> bool:
        ri = row_index(y)
        return buf[ri * pitch + x] > 0

    pixel_on = mono_pixel_on if pixel_mode == freetype.FT_PIXEL_MODE_MONO else gray_pixel_on

    out = bytearray()
    acc = 0
    bits = 0
    for y in range(h):
        for x in range(w):
            acc = (acc << 1) | (1 if pixel_on(x, y) else 0)
            bits += 1
            if bits == 8:
                out.append(acc & 0xFF)
                acc = 0
                bits = 0

    if bits:
        acc <<= (8 - bits)
        out.append(acc & 0xFF)

    return bytes(out), w, h


def parse_int_auto_base(s: str) -> int:
    """Parse int from decimal or hex (0xNN)."""
    s = s.strip().lower()
    base = 16 if s.startswith("0x") else 10
    return int(s, base)


def pick_ttf(ttf_arg: Optional[str], search_dir: Path) -> Path:
    if ttf_arg:
        p = Path(ttf_arg).expanduser()
        if not p.is_absolute():
            p = (search_dir / p).resolve()
        if not p.exists():
            raise SystemExit(f"TTF not found: {p}")
        return p

    ttf_files = sorted(search_dir.glob("*.ttf"))
    if not ttf_files:
        raise SystemExit(f"No .ttf found in: {search_dir}")
    if len(ttf_files) > 1:
        print("Multiple .ttf files found; using:", ttf_files[0].name)
    return ttf_files[0]


def find_ttfs(search_dir: Path) -> List[Path]:
    return sorted(
        p
        for p in search_dir.iterdir()
        if p.is_file() and p.suffix.lower() == ".ttf"
    )


def resolve_ttf_search_dir(script_dir: Path) -> Path:
    candidate = (script_dir / DEFAULT_TTF_SUBDIR).resolve()
    return candidate if candidate.is_dir() else script_dir


def resolve_default_out_root(script_dir: Path) -> Path:
    project_root = script_dir.parent.parent
    return (project_root / "include" / DEFAULT_OUT_ROOT_SUBDIR).resolve()


def dedupe_symbol(base: str, used: set[str]) -> str:
    if base not in used:
        return base
    i = 2
    while f"{base}_{i}" in used:
        i += 1
    return f"{base}_{i}"


def _mtime(path: Path) -> float:
    try:
        return path.stat().st_mtime
    except FileNotFoundError:
        return 0.0


def parse_sizes_arg(s: str) -> List[int]:
    """Parse comma-separated sizes, e.g. '9,12,15,18,24'."""
    raw = [p.strip() for p in s.split(",") if p.strip()]
    if not raw:
        raise ValueError("Empty --sizes")
    sizes: List[int] = []
    for part in raw:
        v = int(part)
        if v <= 0:
            raise ValueError(f"Invalid size: {v}")
        sizes.append(v)
    # de-dup preserving order
    out: List[int] = []
    seen = set()
    for v in sizes:
        if v not in seen:
            out.append(v)
            seen.add(v)
    return out


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Generate TFT_eSPI/Adafruit_GFX FreeFont header from TTF")
    p.add_argument(
        "--scan-dir",
        type=str,
        default=None,
        help="Scan an existing font-header directory and generate all_fonts.h + fonts_index.h",
    )
    p.add_argument(
        "--ttf",
        type=str,
        default=None,
        help="Path to .ttf (default: first *.ttf in Add_New_ttf_fonts here, else script folder)",
    )
    p.add_argument("--size", type=int, default=DEFAULT_SIZE_PT, help="Single font size in points")
    p.add_argument("--sizes", type=str, default=None, help="Comma-separated sizes, e.g. 9,12,15,18,24")
    p.add_argument("--min-size", type=int, default=None, help="Generate a size range: minimum point size")
    p.add_argument("--max-size", type=int, default=None, help="Generate a size range: maximum point size")
    p.add_argument("--step", type=int, default=1, help="Step for size range (default: 1)")
    p.add_argument("--dpi", type=int, default=DEFAULT_DPI, help="DPI used for point-size scaling")
    p.add_argument("--first", type=parse_int_auto_base, default=DEFAULT_FIRST, help="First char code (e.g. 0x20)")
    p.add_argument("--last", type=parse_int_auto_base, default=DEFAULT_LAST, help="Last char code (e.g. 0x7E)")
    p.add_argument(
        "--out-dir",
        type=str,
        default=None,
        help="Output directory (default: include/Generated:Fonts_For_Copy/<font-name>)",
    )
    p.add_argument("--name", type=str, default=None, help="Override C symbol base name (default: derived from TTF stem)")
    return p


def generate_font_header(
    *,
    ttf_path: Path,
    base_name: str,
    size_pt: int,
    dpi: int,
    first: int,
    last: int,
    out_path: Path,
) -> Tuple[str, int]:
    """Generate one .h file. Returns (font_name, bitmap_bytes_len)."""
    font_name = f"{base_name}{size_pt}pt7b"

    # Import lazily so scan/index mode works without freetype-py installed.
    try:
        import freetype  # type: ignore
    except ImportError as e:
        raise SystemExit(
            "Missing dependency: freetype-py. Install with: pip install freetype-py\n"
            f"Details: {e}"
        )

    face = freetype.Face(str(ttf_path))
    face.set_char_size(size_pt * 64, 0, dpi, dpi)

    # freetype-py compatibility: face.size is SizeMetrics
    y_advance = int(face.size.height) >> 6
    if y_advance <= 0:
        y_advance = (int(face.size.ascender) - int(face.size.descender)) >> 6

    bitmap_bytes = bytearray()
    glyph_entries: List[Tuple[int, int, int, int, int, int]] = []
    # (bitmapOffset, width, height, xAdvance, xOffset, yOffset)

    for code in range(first, last + 1):
        # Keep indices aligned even if glyph missing
        if face.get_char_index(code) == 0 and code != 0:
            bitmap_offset = len(bitmap_bytes)
            glyph_entries.append((bitmap_offset, 0, 0, max(1, size_pt // 2), 0, 0))
            continue

        # IMPORTANT: load, then explicitly render to MONO
        face.load_char(chr(code), freetype.FT_LOAD_DEFAULT)
        face.glyph.render(freetype.FT_RENDER_MODE_MONO)

        g = face.glyph
        b = g.bitmap

        packed, w, h = pack_bitmap_to_1bpp(b)

        bitmap_offset = len(bitmap_bytes)
        bitmap_bytes.extend(packed)

        x_advance = int(g.advance.x) >> 6
        x_offset = int(g.bitmap_left)
        # Adafruit_GFX yOffset: baseline -> top of bitmap (negative if above baseline)
        y_offset = -int(g.bitmap_top)

        glyph_entries.append((bitmap_offset, w, h, x_advance, x_offset, y_offset))

    guard = f"_{font_name.upper()}_H_"

    out: List[str] = []
    out.append(f"#ifndef {guard}")
    out.append(f"#define {guard}")
    out.append("")
    out.append("#include <pgmspace.h>")
    out.append("#include <TFT_eSPI.h>  // provides GFXfont and GFXglyph")
    out.append("")
    out.append("// FreeFont (1bpp) generated from TTF by fontMaker.py")
    out.append(f"// Source: {ttf_path.name}")
    out.append(f"// Size: {size_pt}pt @ {dpi}dpi, chars: 0x{first:02X}-0x{last:02X}")
    out.append("")

    out.append(f"const uint8_t {font_name}Bitmaps[] PROGMEM = {{")
    if bitmap_bytes:
        out.append(hex_bytes(bytes(bitmap_bytes)) + ",")
    out.append("};")
    out.append("")

    out.append(f"const GFXglyph {font_name}Glyphs[] PROGMEM = {{")
    for bo, w, h, xa, xo, yo in glyph_entries:
        out.append(f"  {{ {bo}, {w}, {h}, {xa}, {xo}, {yo} }},")
    out.append("};")
    out.append("")

    out.append(f"const GFXfont {font_name} PROGMEM = {{")
    out.append(f"  (uint8_t  *){font_name}Bitmaps,")
    out.append(f"  (GFXglyph *){font_name}Glyphs,")
    out.append(f"  0x{first:02X}, 0x{last:02X}, {y_advance}")
    out.append("};")
    out.append("")
    out.append(f"#endif // {guard}")
    out.append("")

    out_path.write_text("\n".join(out), encoding="utf-8")
    return font_name, len(bitmap_bytes)


def _extract_size_pt_from_name(stem: str) -> Optional[int]:
    m = re.search(r"(\d+)pt7b$", stem)
    if not m:
        return None
    try:
        return int(m.group(1))
    except ValueError:
        return None


def _extract_gfxfont_symbol(header_text: str) -> Optional[str]:
    # Matches: const GFXfont a24pt7b PROGMEM = {
    m = re.search(r"\bconst\s+GFXfont\s+(\w+)\s+PROGMEM\b", header_text)
    if not m:
        return None
    return m.group(1)


def generate_folder_aggregates(scan_dir: Path) -> Tuple[Path, Path, int]:
    """Generate all_fonts.h and fonts_index.h inside scan_dir.

    Returns (all_fonts_path, fonts_index_path, font_count)
    """
    scan_dir = scan_dir.resolve()
    if not scan_dir.exists() or not scan_dir.is_dir():
        raise SystemExit(f"--scan-dir is not a directory: {scan_dir}")

    # Match typical FreeFont filenames: a24pt7b.h
    header_paths = sorted(scan_dir.glob("*pt7b.h"))
    if not header_paths:
        raise SystemExit(f"No '*pt7b.h' headers found in: {scan_dir}")

    items = []
    for p in header_paths:
        size_pt = _extract_size_pt_from_name(p.stem)
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            text = ""
        symbol = _extract_gfxfont_symbol(text)
        if not symbol:
            # Fallback: use stem as symbol (common case), but keep it explicit.
            symbol = p.stem
        items.append(
            {
                "path": p,
                "include": p.name,
                "stem": p.stem,
                "symbol": symbol,
                "size": size_pt,
            }
        )

    # Prefer numeric sort by size when possible, otherwise fall back to name.
    if all(it["size"] is not None for it in items):
        items.sort(key=lambda it: int(it["size"]))
    else:
        items.sort(key=lambda it: str(it["stem"]))

    all_fonts_path = scan_dir / "all_fonts.h"
    fonts_index_path = scan_dir / "fonts_index.h"

    # 1) all_fonts.h
    out = []
    out.append("#pragma once")
    out.append("")
    out.append("// Auto-generated by fontMaker.py --scan-dir")
    out.append(f"// Source directory: {scan_dir}")
    out.append(f"// Headers: {len(items)}")
    out.append("")
    for it in items:
        out.append(f"#include \"{it['include']}\"")
    out.append("")
    all_fonts_path.write_text("\n".join(out), encoding="utf-8")

    # 2) fonts_index.h
    out = []
    out.append("#pragma once")
    out.append("")
    out.append("// Auto-generated by fontMaker.py --scan-dir")
    out.append("#include <TFT_eSPI.h>")
    out.append("")
    out.append("struct FontEntry")
    out.append("{")
    out.append("  const GFXfont *font;")
    out.append("  uint16_t sizePt;")
    out.append("  const char *name;")
    out.append("};")
    out.append("")
    out.append("// Includes are in all_fonts.h (include that before this file)")
    out.append("static const FontEntry kFonts[] = {")
    for it in items:
        size_pt = it["size"] if it["size"] is not None else 0
        out.append(f"  {{ &{it['symbol']}, {int(size_pt)}, \"{it['stem']}\" }},")
    out.append("};")
    out.append("static constexpr size_t kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);")
    out.append("")
    fonts_index_path.write_text("\n".join(out), encoding="utf-8")

    return all_fonts_path, fonts_index_path, len(items)


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    ttf_search_dir = resolve_ttf_search_dir(script_dir)
    default_out_root = resolve_default_out_root(script_dir)

    # If you run the script with no arguments, generate the full range 6..72 for
    # every .ttf in Add_New_ttf_fonts here (fallback: next to this script).
    if len(sys.argv) == 1:
        first = DEFAULT_FIRST
        last = DEFAULT_LAST
        dpi = DEFAULT_DPI
        sizes = list(range(AUTO_MIN_SIZE_PT, AUTO_MAX_SIZE_PT + 1, AUTO_STEP_PT))

        ttf_files = find_ttfs(ttf_search_dir)
        if not ttf_files:
            raise SystemExit(f"No .ttf found in: {ttf_search_dir}")

        print("Range:", f"0x{first:02X}..0x{last:02X}")
        print("DPI :", dpi)
        print("Sizes:", f"{AUTO_MIN_SIZE_PT}..{AUTO_MAX_SIZE_PT} step {AUTO_STEP_PT}")

        used_base_names: set[str] = set()

        for ttf_path in ttf_files:
            base_name = make_c_symbol(ttf_path.stem)
            base_name = dedupe_symbol(base_name, used_base_names)
            used_base_names.add(base_name)

            out_dir = (default_out_root / base_name).resolve() if AUTO_OUT_DIR_IS_FONT_SUBFOLDER else default_out_root
            out_dir.mkdir(parents=True, exist_ok=True)

            print("")
            print("TTF :", ttf_path.name)
            print("OUT :", str(out_dir))

            ttf_mtime = _mtime(ttf_path)
            generated_any = False
            newest_header_mtime = 0.0

            for size_pt in sizes:
                font_name = f"{base_name}{size_pt}pt7b"
                out_path = out_dir / f"{font_name}.h"
                out_mtime = _mtime(out_path)
                newest_header_mtime = max(newest_header_mtime, out_mtime)

                # Only (re)generate if missing or older than the TTF.
                if out_mtime >= ttf_mtime and out_mtime > 0:
                    continue

                gen_name, bitmap_len = generate_font_header(
                    ttf_path=ttf_path,
                    base_name=base_name,
                    size_pt=size_pt,
                    dpi=dpi,
                    first=first,
                    last=last,
                    out_path=out_path,
                )
                generated_any = True
                newest_header_mtime = max(newest_header_mtime, _mtime(out_path))
                print(f"- {gen_name}: {bitmap_len} bytes -> {out_path.name}")

            # Generate/refresh index headers only if needed.
            all_fonts_path = out_dir / "all_fonts.h"
            fonts_index_path = out_dir / "fonts_index.h"
            all_fonts_mtime = _mtime(all_fonts_path)
            fonts_index_mtime = _mtime(fonts_index_path)
            need_index = (
                generated_any
                or all_fonts_mtime == 0.0
                or fonts_index_mtime == 0.0
                or all_fonts_mtime < newest_header_mtime
                or fonts_index_mtime < newest_header_mtime
            )

            if need_index:
                all_fonts_path, fonts_index_path, count = generate_folder_aggregates(out_dir)
                print(f"INDEX: {count} font(s)")
                print(f"WROTE: {all_fonts_path.name}")
                print(f"WROTE: {fonts_index_path.name}")
            else:
                print("SKIP: up-to-date")

        return

    args = build_arg_parser().parse_args()

    # Scan/index mode (no FreeType dependency)
    if args.scan_dir:
        scan_dir = Path(args.scan_dir).expanduser()
        if not scan_dir.is_absolute():
            scan_dir = (script_dir / scan_dir).resolve()
        all_fonts_path, fonts_index_path, count = generate_folder_aggregates(scan_dir)
        print("SCAN:", str(scan_dir))
        print("WROTE:", all_fonts_path.name)
        print("WROTE:", fonts_index_path.name)
        print("FONTS:", count)
        return

    sizes: List[int]
    if args.sizes and (args.min_size is not None or args.max_size is not None):
        raise SystemExit("Use either --sizes or --min-size/--max-size, not both")

    if args.min_size is not None or args.max_size is not None:
        if args.min_size is None or args.max_size is None:
            raise SystemExit("Both --min-size and --max-size are required")
        mn = int(args.min_size)
        mx = int(args.max_size)
        step = int(args.step)
        if mn <= 0 or mx <= 0 or mn > mx:
            raise SystemExit("Invalid --min-size/--max-size")
        if step <= 0:
            raise SystemExit("--step must be > 0")
        sizes = list(range(mn, mx + 1, step))
    elif args.sizes:
        try:
            sizes = parse_sizes_arg(args.sizes)
        except ValueError as e:
            raise SystemExit(str(e))
    else:
        size_pt = int(args.size)
        if size_pt <= 0:
            raise SystemExit("--size must be > 0")
        sizes = [size_pt]

    first = int(args.first)
    last = int(args.last)
    if not (0 <= first <= 0x10FFFF and 0 <= last <= 0x10FFFF and first <= last):
        raise SystemExit("Invalid --first/--last range")

    dpi = int(args.dpi)
    if dpi <= 0:
        raise SystemExit("--dpi must be > 0")

    ttf_path = pick_ttf(args.ttf, ttf_search_dir)

    base_name = make_c_symbol(args.name) if args.name else make_c_symbol(ttf_path.stem)

    if args.out_dir:
        out_dir = Path(args.out_dir).expanduser()
    else:
        out_dir = (default_out_root / base_name) if AUTO_OUT_DIR_IS_FONT_SUBFOLDER else default_out_root
    if not out_dir.is_absolute():
        out_dir = (script_dir / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    print("TTF :", ttf_path.name)
    print("OUT :", str(out_dir))
    print("Range:", f"0x{first:02X}..0x{last:02X}")
    print("DPI :", dpi)

    for size_pt in sizes:
        font_name = f"{base_name}{size_pt}pt7b"
        out_path = out_dir / f"{font_name}.h"
        gen_name, bitmap_len = generate_font_header(
            ttf_path=ttf_path,
            base_name=base_name,
            size_pt=size_pt,
            dpi=dpi,
            first=first,
            last=last,
            out_path=out_path,
        )
        print(f"- {gen_name}: {bitmap_len} bytes -> {out_path.name}")

    # After any generation run, create/update aggregator + index headers.
    all_fonts_path, fonts_index_path, count = generate_folder_aggregates(out_dir)
    print(f"INDEX: {count} font(s)")
    print(f"WROTE: {all_fonts_path.name}")
    print(f"WROTE: {fonts_index_path.name}")


if __name__ == "__main__":
    main()
