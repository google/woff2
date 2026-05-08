#!/usr/bin/env python3
"""Generate TTF fixtures exercising issue #191 (unbounded ReadGlyph allocation).

Produces:
  valid_baseline.ttf            — a legitimate TTF with monotonic endPts.
  non_monotonic_endpts.ttf      — one glyph with endPtsOfContours == [5, 2].
  non_monotonic_one_step.ttf    — endPts == [1, 0] (smallest wrap-to-65535 case).
  non_monotonic_large_drop.ttf  — endPts == [0xFFFE, 0] (extreme downward jump).
  dos_reproducer.ttf            — many glyphs each crafted with wraparound.

Each "non-monotonic" / "dos" fixture is built by starting from a valid font
and surgically rewriting the glyf/loca tables so the arithmetic problem
described in PLAN.md section 1 appears exactly where ReadGlyph reads it.
"""

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont
import io
import os
import struct
import sys


def build_valid_font(num_glyphs=4):
    """Return a minimal but spec-complete TTF as bytes."""
    fb = FontBuilder(1024, isTTF=True)
    glyph_names = [".notdef"] + [f"g{i}" for i in range(1, num_glyphs)]
    fb.setupGlyphOrder(glyph_names)
    fb.setupCharacterMap({ord("A") + i - 1: glyph_names[i]
                          for i in range(1, num_glyphs)})

    glyphs = {}
    for name in glyph_names:
        pen = TTGlyphPen(None)
        # Draw a simple square so each glyph has at least one contour.
        pen.moveTo((0, 0))
        pen.lineTo((100, 0))
        pen.lineTo((100, 100))
        pen.lineTo((0, 100))
        pen.closePath()
        glyphs[name] = pen.glyph()
    fb.setupGlyf(glyphs)

    metrics = {name: (500, 0) for name in glyph_names}
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800,
                usWinDescent=200)
    fb.setupNameTable({"familyName": "Test", "styleName": "Regular"})
    fb.setupPost()

    buf = io.BytesIO()
    fb.save(buf)
    return buf.getvalue()


def rewrite_glyph(font_bytes, glyph_index, new_glyph_bytes):
    """Replace the raw bytes of one glyph record in an sfnt.

    Reads the compiled font's glyf + loca tables, splices the new bytes into
    the glyf table, rewrites loca to reflect the new offsets, and rebuilds the
    sfnt via SFNTWriter so the raw bytes survive serialisation unchanged.
    """
    from fontTools.ttLib.sfnt import SFNTReader, SFNTWriter

    src = io.BytesIO(font_bytes)
    reader = SFNTReader(src)
    num_glyphs = TTFont(io.BytesIO(font_bytes))["maxp"].numGlyphs
    head_table = TTFont(io.BytesIO(font_bytes))["head"]
    if head_table.indexToLocFormat == 0:
        fmt_char = "H"
        scale = 2
    else:
        fmt_char = "I"
        scale = 1

    glyf_data = reader["glyf"]
    loca_data = reader["loca"]
    loca_fmt = ">" + fmt_char * (num_glyphs + 1)
    loca_values = list(struct.unpack(loca_fmt, loca_data))
    start = loca_values[glyph_index] * scale
    end = loca_values[glyph_index + 1] * scale

    new_glyf = (glyf_data[:start] + new_glyph_bytes + glyf_data[end:])
    # Keep glyf padded to 4 bytes (sfnt convention for table data).
    pad = (-len(new_glyf)) & 3
    new_glyf += b"\x00" * pad

    delta_bytes = len(new_glyph_bytes) - (end - start)
    delta_units = delta_bytes // scale  # works for both short and long loca

    new_loca_values = list(loca_values)
    for i in range(glyph_index + 1, num_glyphs + 1):
        new_loca_values[i] = loca_values[i] + delta_units
    new_loca_bytes = struct.pack(loca_fmt, *new_loca_values)

    out = io.BytesIO()
    writer = SFNTWriter(out, numTables=len(reader.tables),
                        sfntVersion=reader.sfntVersion)
    for tag in reader.tables:
        data = reader[tag]
        if tag == "glyf":
            data = new_glyf
        elif tag == "loca":
            data = new_loca_bytes
        writer[tag] = data
    writer.close()
    return out.getvalue()


def simple_glyph_bytes(endpts, num_points_override=None,
                       include_coords=True, trailing_padding=0):
    """Build the raw bytes of a simple glyph record.

    endpts: list of uint16 endPtsOfContours values (may be non-monotonic).
    num_points_override: if given, writes exactly that many flag/x/y bytes.
        Otherwise derives from endpts[-1] + 1 when include_coords is True.
    """
    buf = struct.pack(">hhhhh", len(endpts), 0, 0, 100, 100)
    for ep in endpts:
        buf += struct.pack(">H", ep & 0xFFFF)
    buf += struct.pack(">H", 0)  # instructionLength
    if include_coords:
        n = (num_points_override if num_points_override is not None
             else (endpts[-1] + 1 if endpts else 0))
        # flag 0x37: on-curve, x short (+), y short (+), no repeat
        buf += bytes([0x37]) * n
        buf += bytes([1]) * n  # x coords
        buf += bytes([1]) * n  # y coords
    buf += b"\x00" * trailing_padding
    return buf


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__)
    os.makedirs(out_dir, exist_ok=True)

    def write(name, data):
        path = os.path.join(out_dir, name)
        with open(path, "wb") as f:
            f.write(data)
        print(f"wrote {path} ({len(data)} bytes)")

    # 1. Baseline valid TTF — monotonic endpoints throughout.
    baseline = build_valid_font()
    write("valid_baseline.ttf", baseline)

    # 2. Non-monotonic endpoints [5, 2] on glyph index 1.
    mal1 = rewrite_glyph(baseline, 1, simple_glyph_bytes([5, 2],
                                                          include_coords=False,
                                                          trailing_padding=6))
    write("non_monotonic_endpts.ttf", mal1)

    # 3. Minimal wrap: endpts = [1, 0].
    mal2 = rewrite_glyph(baseline, 1, simple_glyph_bytes([1, 0],
                                                          include_coords=False,
                                                          trailing_padding=6))
    write("non_monotonic_one_step.ttf", mal2)

    # 4. Extreme drop: endpts = [0xFFFE, 0].
    mal3 = rewrite_glyph(baseline, 1, simple_glyph_bytes([0xFFFE, 0],
                                                          include_coords=False,
                                                          trailing_padding=6))
    write("non_monotonic_large_drop.ttf", mal3)

    # 5. DoS reproducer: every writable glyph has a wraparound pattern.
    dos = baseline
    font = TTFont(io.BytesIO(baseline))
    num_glyphs = font["maxp"].numGlyphs
    for i in range(1, num_glyphs):
        dos = rewrite_glyph(dos, i, simple_glyph_bytes([100, 0],
                                                        include_coords=False,
                                                        trailing_padding=4))
    write("dos_reproducer.ttf", dos)


if __name__ == "__main__":
    main()
