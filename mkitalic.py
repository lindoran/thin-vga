#!/usr/bin/env python3
"""
mkitalic.py  --  Generate font_italic.h for thin-vga

Renders FreeMonoOblique (or DejaVuSansMono-Oblique) at the right point
size to fill 8x16 cells for ASCII 0x20-0x7E.  CP437 special characters
(0x00-0x1F and 0x7F-0xFF) are taken from font_vga.h and algorithmically
slanted, since those glyphs have no TTF equivalent.

Usage:
    python3 mkitalic.py [font.ttf] > font_italic.h
    python3 mkitalic.py --preview   (saves italic_preview.png, no header)

Output:
    C header in the same format as font_vga.h, array named
    vga_font_italic_8x16[256][16].
"""

import sys
import os
import struct

# ---------------------------------------------------------------------------
# CP437 -> Unicode map (same table mkfont.py uses, needed for comments)
# ---------------------------------------------------------------------------
CP437_UNICODE = [
    0x0000,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,
    0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
    0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,
    0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC,
    # 0x20-0x7E: standard ASCII
    *range(0x0020, 0x007F),
    0x2302,  # 0x7F house
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,
    0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,
    0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,
    0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
    0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,
    0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,
    0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,
    0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,
    0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,
]

# ---------------------------------------------------------------------------
# Parse font_vga.h to get the base glyphs for the special chars
# ---------------------------------------------------------------------------
def load_vga_font(path):
    """Return list of 256 glyphs, each a list of 16 ints."""
    glyphs = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith('{'):
                continue
            nums = line.split('{')[1].split('}')[0]
            vals = [int(x.strip(), 16) for x in nums.split(',')]
            if len(vals) == 16:
                glyphs.append(vals)
    assert len(glyphs) == 256, f"Expected 256 glyphs, got {len(glyphs)}"
    return glyphs

# ---------------------------------------------------------------------------
# Algorithmic italic slant: shift row r right by (15-r)//4 bits
# Used for CP437 special glyphs that have no TTF equivalent
# ---------------------------------------------------------------------------
def slant_glyph(glyph):
    """Apply a rightward slant to an 8x16 bitmap glyph."""
    result = []
    for r, byte in enumerate(glyph):
        shift = (15 - r) >> 2   # 0,1,2,3 from bottom to top
        # shift right means lower pixel positions (MSB = leftmost)
        slanted = (byte >> shift) & 0xFF
        result.append(slanted)
    return result

# ---------------------------------------------------------------------------
# Render TTF italic glyphs for ASCII 0x20-0x7E
# ---------------------------------------------------------------------------
def render_ttf_glyphs(font_path, cell_w=8, cell_h=16):
    """
    Render ASCII 0x20..0x7E from font_path into 8x16 bitmaps.
    Returns a dict: codepoint -> list of 16 ints (MSB=leftmost pixel).
    """
    try:
        from PIL import Image, ImageFont, ImageDraw
    except ImportError:
        print("# WARNING: Pillow not available, falling back to slant algorithm for all glyphs",
              file=sys.stderr)
        return {}

    font = ImageFont.truetype(font_path, 13)
    asc, desc = font.getmetrics()
    # vertical offset to centre the glyph in the cell
    y_off = max(0, (cell_h - asc - desc) // 2)

    result = {}
    for cp in range(0x20, 0x7F):
        ch = chr(cp)
        img = Image.new('L', (cell_w * 2, cell_h), 0)   # wider canvas for italic overhang
        draw = ImageDraw.Draw(img)
        draw.text((0, y_off), ch, font=font, fill=255)

        rows = []
        for r in range(cell_h):
            byte = 0
            for b in range(cell_w):
                px = img.getpixel((b, r))
                if px > 64:          # threshold
                    byte |= (0x80 >> b)
            rows.append(byte)
        result[cp] = rows

    return result

# ---------------------------------------------------------------------------
# Build the preview PNG
# ---------------------------------------------------------------------------
def save_preview(glyphs, path='italic_preview.png'):
    try:
        from PIL import Image
    except ImportError:
        print("Pillow not available, skipping preview.", file=sys.stderr)
        return

    cols, rows = 32, 8
    scale = 3
    img = Image.new('RGB', (cols * 8 * scale, rows * 16 * scale), (0x18, 0x18, 0x18))
    pixels = img.load()

    for idx in range(256):
        glyph = glyphs[idx]
        col = idx % cols
        row = idx // cols
        bx = col * 8 * scale
        by = row * 16 * scale
        for r, byte in enumerate(glyph):
            for b in range(8):
                lit = bool(byte & (0x80 >> b))
                color = (0xFF, 0xFF, 0xFF) if lit else (0x18, 0x18, 0x18)
                for sy in range(scale):
                    for sx in range(scale):
                        pixels[bx + b*scale + sx, by + r*scale + sy] = color

    img.save(path)
    print(f"Preview saved to {path}", file=sys.stderr)

# ---------------------------------------------------------------------------
# Emit the C header
# ---------------------------------------------------------------------------
def emit_header(glyphs, font_path):
    print("/* font_italic.h  --  VGA 8x16 italic font bitmap table")
    print(" *")
    print(f" * Auto-generated by mkitalic.py")
    print(f" * TTF source: {os.path.basename(font_path)}")
    print(f" * ASCII 0x20-0x7E: rendered from TTF at pt 13")
    print(f" * Other ranges:    slanted from font_vga.h (shift = (15-row)>>2)")
    print(" *")
    print(" * Each row is 8 pixels wide; MSB is the leftmost pixel.")
    print(" * Index with:  vga_font_italic_8x16[cp437_byte][scanline_row]")
    print(" *")
    print(" * Regenerate:")
    print(" *   python3 mkitalic.py > font_italic.h")
    print(" */")
    print()
    print("#ifndef FONT_ITALIC_H")
    print("#define FONT_ITALIC_H")
    print()
    print("/* 256 x 16 bytes = 4096 bytes total */")
    print("static const unsigned char vga_font_italic_8x16[256][16] = {")

    for i, glyph in enumerate(glyphs):
        hex_vals = ", ".join(f"0x{v:02X}" for v in glyph)
        u = CP437_UNICODE[i] if i < len(CP437_UNICODE) else 0
        print(f"    {{ {hex_vals} }},  /* 0x{i:02X} U+{u:04X} */")

    print("};")
    print()
    print("#endif /* FONT_ITALIC_H */")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    preview_mode = '--preview' in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith('--')]

    # Locate font
    candidates = [
        args[0] if args else None,
        '/usr/share/fonts/truetype/freefont/FreeMonoOblique.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf',
        '/usr/share/fonts/truetype/liberation/LiberationMono-Italic.ttf',
    ]
    font_path = None
    for c in candidates:
        if c and os.path.exists(c):
            font_path = c
            break

    if font_path is None:
        print("# ERROR: no italic monospace TTF found; install freefont-ttf or dejavu-ttf",
              file=sys.stderr)
        sys.exit(1)

    print(f"Using font: {font_path}", file=sys.stderr)

    # Locate font_vga.h for base glyphs
    vga_paths = [
        'deps/thin-vga/font_vga.h',
        'font_vga.h',
        '../thin-vga/font_vga.h',
    ]
    vga_path = None
    for p in vga_paths:
        if os.path.exists(p):
            vga_path = p
            break

    if vga_path is None:
        print("# ERROR: font_vga.h not found; run from the macguffin root directory",
              file=sys.stderr)
        sys.exit(1)

    print(f"Base font:  {vga_path}", file=sys.stderr)
    base = load_vga_font(vga_path)

    # Render TTF for ASCII printable range
    ttf_glyphs = render_ttf_glyphs(font_path)

    # Build full 256-glyph italic table
    glyphs = []
    for i in range(256):
        if i in ttf_glyphs:
            glyphs.append(ttf_glyphs[i])
        else:
            # CP437 special / non-ASCII: slant the VGA bitmap
            glyphs.append(slant_glyph(base[i]))

    if preview_mode:
        save_preview(glyphs, 'italic_preview.png')
        return

    emit_header(glyphs, font_path)

if __name__ == '__main__':
    main()
