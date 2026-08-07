#!/usr/bin/env python3
"""Generate font/sprite data tables from readable ASCII-art sources.

Reads   tools/fonts/font5x7.txt, font10x16.txt, icons.txt
Writes  core/include/phoenix/font_data.h        (declarations)
        core/src/generated/font_data.cpp        (C++ tables)
        ios/Phoenix/Generated/FontData.swift    (Swift tables for the app's
                                                 virtual glasses renderer)

The generated files are committed; rerun this script after editing a source.
Bitmap layout (both languages): per glyph/sprite, row-major, MSB-first within
each byte, each row padded to a whole number of bytes. This matches
phoenix::Sprite and the font renderer in core/src/font.cpp.
"""

import os
import re
import sys

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FONT_SOURCES = ["tools/fonts/font5x7.txt", "tools/fonts/font10x16.txt"]
SPRITE_SOURCES = ["tools/fonts/icons.txt"]
OUT_HEADER = "core/include/phoenix/font_data.h"
OUT_CPP = "core/src/generated/font_data.cpp"
OUT_SWIFT = "ios/Phoenix/Generated/FontData.swift"

FONT_RE = re.compile(
    r"^font\s+(\w+)\s+height=(\d+)\s+tracking=(\d+)\s+fallback=0x([0-9A-Fa-f]{2})$"
)
GLYPH_RE = re.compile(r"^glyph\s+0x([0-9A-Fa-f]{2})\s+'(.*)'\s+width=(\d+)$")
SPRITE_RE = re.compile(r"^sprite\s+([A-Z0-9_]+)\s+(\d+)x(\d+)$")


class SourceError(Exception):
    pass


def read_lines(path):
    with open(path, "r", encoding="utf-8") as f:
        for num, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#") and not set(line.strip()) <= set("#."):
                # Comment lines start with '#' but so can bitmap rows; a bitmap
                # row consists solely of '.' and '#'. Anything else starting
                # with '#' is a comment.
                if set(line.strip()) <= set("#.") and line.strip():
                    yield num, line
                continue
            yield num, line


def take_rows(lines, count, width, where):
    rows = []
    for _ in range(count):
        try:
            num, line = next(lines)
        except StopIteration:
            raise SourceError(f"{where}: expected {count} bitmap rows, file ended")
        row = line.strip()
        if len(row) != width or not set(row) <= set(".#"):
            raise SourceError(
                f"{where} line {num}: bad row {row!r} (need exactly {width} chars of . and #)"
            )
        rows.append(row)
    return rows


def pack_rows(rows, width):
    data = []
    bpr = (width + 7) // 8
    for row in rows:
        rowbytes = [0] * bpr
        for col, ch in enumerate(row):
            if ch == "#":
                rowbytes[col // 8] |= 0x80 >> (col % 8)
        data.extend(rowbytes)
    return data


def parse_font(path):
    lines = read_lines(path)
    font = None
    glyphs = {}
    for num, line in lines:
        m = FONT_RE.match(line.strip())
        if m:
            if font is not None:
                raise SourceError(f"{path}:{num}: second font header")
            font = {
                "name": m.group(1),
                "height": int(m.group(2)),
                "tracking": int(m.group(3)),
                "fallback": int(m.group(4), 16),
            }
            continue
        m = GLYPH_RE.match(line.strip())
        if m:
            if font is None:
                raise SourceError(f"{path}:{num}: glyph before font header")
            code = int(m.group(1), 16)
            width = int(m.group(3))
            if code in glyphs:
                raise SourceError(f"{path}:{num}: duplicate glyph 0x{code:02X}")
            if not 1 <= width <= 16:
                raise SourceError(f"{path}:{num}: width {width} out of range")
            rows = take_rows(lines, font["height"], width, f"{path} glyph 0x{code:02X}")
            glyphs[code] = {"width": width, "rows": rows, "label": m.group(2)}
            continue
        raise SourceError(f"{path}:{num}: unrecognized line {line!r}")
    if font is None or not glyphs:
        raise SourceError(f"{path}: no font/glyphs found")
    if font["fallback"] not in glyphs:
        raise SourceError(f"{path}: fallback 0x{font['fallback']:02X} has no glyph")
    font["glyphs"] = glyphs
    font["first"] = min(glyphs)
    font["last"] = max(glyphs)
    return font


def parse_sprites(path):
    lines = read_lines(path)
    sprites = []
    seen = set()
    for num, line in lines:
        m = SPRITE_RE.match(line.strip())
        if not m:
            raise SourceError(f"{path}:{num}: unrecognized line {line!r}")
        name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
        if name in seen:
            raise SourceError(f"{path}:{num}: duplicate sprite {name}")
        if not (1 <= w <= 32 and 1 <= h <= 32):
            raise SourceError(f"{path}:{num}: sprite {name} size out of range")
        seen.add(name)
        rows = take_rows(lines, h, w, f"{path} sprite {name}")
        sprites.append({"name": name, "width": w, "height": h, "rows": rows})
    return sprites


def camel(name):  # ICON_BELL -> IconBell
    return "".join(p.capitalize() for p in name.lower().split("_"))


def build_font_tables(font):
    """Returns (glyph_infos, bitmap, per_glyph_comments)."""
    infos = []
    bitmap = []
    comments = []
    for code in range(font["first"], font["last"] + 1):
        g = font["glyphs"].get(code)
        if g is None:
            infos.append((0, 0))  # width 0 == absent -> renderer falls back
            continue
        offset = len(bitmap)
        if offset > 0xFFFF:
            raise SourceError("bitmap exceeds 16-bit offsets")
        infos.append((offset, g["width"]))
        comments.append((offset, code, g["label"], g["width"]))
        bitmap.extend(pack_rows(g["rows"], g["width"]))
    return infos, bitmap, comments


def fmt_bytes(data, indent, per_line=12):
    out = []
    for i in range(0, len(data), per_line):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i : i + per_line])
        out.append(f"{indent}{chunk},")
    return "\n".join(out)


def gen_cpp(fonts, sprites):
    hdr = [
        "#pragma once",
        "",
        "// GENERATED by tools/gen_fonts.py from tools/fonts/*.txt — do not edit.",
        "// Edit the ASCII-art sources and rerun the generator instead.",
        "",
        '#include "phoenix/font.h"',
        '#include "phoenix/framebuffer.h"',
        "",
        "namespace phoenix {",
        "",
    ]
    for f in fonts:
        hdr.append(f"extern const Font kFont{f['name'].capitalize()};")
    hdr.append("")
    for s in sprites:
        hdr.append(f"extern const Sprite k{camel(s['name'])};")
    hdr += ["", "}  // namespace phoenix", ""]

    cpp = [
        "// GENERATED by tools/gen_fonts.py from tools/fonts/*.txt — do not edit.",
        "// Edit the ASCII-art sources and rerun the generator instead.",
        "",
        '#include "phoenix/font_data.h"',
        "",
        "namespace phoenix {",
        "namespace {",
        "",
    ]
    for f in fonts:
        name = f["name"].capitalize()
        infos, bitmap, comments = build_font_tables(f)
        cpp.append(f"// {f['name']}: {len(f['glyphs'])} glyphs, "
                   f"chars 0x{f['first']:02X}-0x{f['last']:02X}, {len(bitmap)} bytes")
        cpp.append(f"const uint8_t k{name}Bitmap[] = {{")
        ci = 0
        for code in range(f["first"], f["last"] + 1):
            g = f["glyphs"].get(code)
            if g is None:
                continue
            offset, _, label, width = comments[ci]
            ci += 1
            cpp.append(f"    // 0x{code:02X} {label!r} w={width} @{offset}")
            cpp.append(fmt_bytes(pack_rows(g["rows"], g["width"]), "    "))
        cpp.append("};")
        cpp.append(f"const GlyphInfo k{name}Glyphs[] = {{")
        line = []
        for off, w in infos:
            line.append(f"{{{off}, {w}}}")
            if len(line) == 8:
                cpp.append("    " + ", ".join(line) + ",")
                line = []
        if line:
            cpp.append("    " + ", ".join(line) + ",")
        cpp.append("};")
        cpp.append("")
    for s in sprites:
        cpp.append(f"const uint8_t k{camel(s['name'])}Data[] = {{")
        cpp.append(fmt_bytes(pack_rows(s["rows"], s["width"]), "    "))
        cpp.append("};")
    cpp += ["", "}  // namespace", ""]
    for f in fonts:
        name = f["name"].capitalize()
        cpp.append(
            f"const Font kFont{name}{{{f['height']}, 0x{f['first']:02X}, 0x{f['last']:02X}, "
            f"{f['tracking']}, 0x{f['fallback']:02X}, k{name}Glyphs, k{name}Bitmap}};"
        )
    cpp.append("")
    for s in sprites:
        n = camel(s["name"])
        cpp.append(f"const Sprite k{n}{{{s['width']}, {s['height']}, k{n}Data}};")
    cpp += ["", "}  // namespace phoenix", ""]
    return "\n".join(hdr), "\n".join(cpp)


def gen_swift(fonts, sprites):
    out = [
        "// GENERATED by tools/gen_fonts.py from tools/fonts/*.txt — do not edit.",
        "// Edit the ASCII-art sources and rerun the generator instead.",
        "",
        "import Foundation",
        "",
        "struct PhoenixGlyph {",
        "    let offset: Int",
        "    let width: Int",
        "}",
        "",
        "struct PhoenixFont {",
        "    let height: Int",
        "    let firstChar: UInt8",
        "    let lastChar: UInt8",
        "    let tracking: Int",
        "    let fallbackChar: UInt8",
        "    let glyphs: [PhoenixGlyph]",
        "    let bitmap: [UInt8]",
        "}",
        "",
        "struct PhoenixSprite {",
        "    let width: Int",
        "    let height: Int",
        "    let data: [UInt8]",
        "}",
        "",
        "enum PhoenixFontData {",
    ]
    for f in fonts:
        infos, bitmap, _ = build_font_tables(f)
        glyphs = ", ".join(f"PhoenixGlyph(offset: {o}, width: {w})" for o, w in infos)
        bts = ", ".join(str(b) for b in bitmap)
        out.append(f"    static let {f['name']} = PhoenixFont(")
        out.append(f"        height: {f['height']}, firstChar: 0x{f['first']:02X}, "
                   f"lastChar: 0x{f['last']:02X}, tracking: {f['tracking']}, "
                   f"fallbackChar: 0x{f['fallback']:02X},")
        out.append(f"        glyphs: [{glyphs}],")
        out.append(f"        bitmap: [{bts}])")
    out.append("    static let sprites: [String: PhoenixSprite] = [")
    for s in sprites:
        data = ", ".join(str(b) for b in pack_rows(s["rows"], s["width"]))
        out.append(f'        "{s["name"]}": PhoenixSprite(width: {s["width"]}, '
                   f"height: {s['height']}, data: [{data}]),")
    out.append("    ]")
    out.append("}")
    out.append("")
    return "\n".join(out)


def write(path, content):
    full = os.path.join(REPO_ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"wrote {path} ({len(content)} bytes)")


def main():
    fonts = [parse_font(os.path.join(REPO_ROOT, p)) for p in FONT_SOURCES]
    sprites = []
    for p in SPRITE_SOURCES:
        sprites.extend(parse_sprites(os.path.join(REPO_ROOT, p)))
    hdr, cpp = gen_cpp(fonts, sprites)
    write(OUT_HEADER, hdr)
    write(OUT_CPP, cpp)
    write(OUT_SWIFT, gen_swift(fonts, sprites))
    for f in fonts:
        print(f"font {f['name']}: {len(f['glyphs'])} glyphs, "
              f"0x{f['first']:02X}-0x{f['last']:02X}")
    print(f"sprites: {len(sprites)}")


if __name__ == "__main__":
    try:
        main()
    except SourceError as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)
