#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
mkfont.py  --  generate font_vga.h from system PSF console fonts.

Merges all *VGA16*.psf.gz fonts found on the system into a complete 256-entry
CP437 glyph table, using the PSF Unicode tables to map code points correctly.

Output goes to stdout; redirect to font_vga.h:
    python3 mkfont.py > font_vga.h

Requires: kbd or console-setup package (for the PSF fonts)
  Debian/Ubuntu:  sudo apt install kbd
  Fedora/RHEL:    sudo dnf install kbd
  Arch:           sudo pacman -S kbd
"""

import gzip
import glob
import struct
import sys
import os

# CP437 byte value -> Unicode code point mapping
# (standard IBM Code Page 437)
CP437_TO_UNICODE = [
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x2302,
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
]
assert len(CP437_TO_UNICODE) == 256

# Hand-drawn fallback bitmaps for glyphs not found in any system font
FALLBACK_BITMAPS = {
    # U+0000 NUL -- blank
    0x0000: bytes(16),
    # U+25BA ► right-pointing solid arrowhead
    0x25BA: bytes([0x00,0x00,0x20,0x30,0x38,0x3C,0x3E,0x3F,
                   0x3E,0x3C,0x38,0x30,0x20,0x00,0x00,0x00]),
    # U+25C4 ◄ left-pointing solid arrowhead
    0x25C4: bytes([0x00,0x00,0x04,0x0C,0x1C,0x3C,0x7C,0xFC,
                   0x7C,0x3C,0x1C,0x0C,0x04,0x00,0x00,0x00]),
}

SEARCH_PATHS = [
    '/usr/share/consolefonts',
    '/usr/share/kbd/consolefonts',
    '/lib/kbd/consolefonts',
]


def load_psf1(path):
    """Load a PSF1 font file (.psf or .psf.gz).
    Returns dict: unicode_codepoint -> 16-byte bitmap, or {} on error."""
    try:
        opener = gzip.open if path.endswith('.gz') else open
        with opener(path, 'rb') as f:
            raw = f.read()
    except Exception as e:
        print(f"# Warning: could not read {path}: {e}", file=sys.stderr)
        return {}

    if raw[:2] != b'\x36\x04':
        return {}  # not PSF1

    mode = raw[2]
    charsize = raw[3]
    num_glyphs = 512 if (mode & 1) else 256
    has_unicode = bool(mode & 2)

    glyph_data = raw[4:4 + num_glyphs * charsize]
    result = {}

    if has_unicode:
        ut = raw[4 + num_glyphs * charsize:]
        i = 0
        glyph_idx = 0
        while i + 1 < len(ut) and glyph_idx < num_glyphs:
            bitmap = glyph_data[glyph_idx * charsize:(glyph_idx + 1) * charsize]
            while i + 1 < len(ut):
                cp = struct.unpack_from('<H', ut, i)[0]
                i += 2
                if cp == 0xFFFF:
                    break
                elif cp == 0xFFFE:
                    continue
                else:
                    if cp not in result:
                        result[cp] = bitmap
            glyph_idx += 1
    return result


def load_psf2(path):
    """Load a PSF2 font file (.psf or .psf.gz).
    Returns dict: unicode_codepoint -> height-byte bitmap, or {} on error."""
    try:
        opener = gzip.open if path.endswith('.gz') else open
        with opener(path, 'rb') as f:
            raw = f.read()
    except Exception:
        return {}

    PSF2_MAGIC = b'\x72\xb5\x4a\x86'
    if raw[:4] != PSF2_MAGIC:
        return {}

    (version, headersize, flags, num_glyphs,
     bytes_per_glyph, height, width) = struct.unpack('<IIIIIII', raw[4:32])

    if height != 16 or width != 8:
        return {}  # we only want 8x16

    has_unicode = bool(flags & 1)
    glyph_data = raw[headersize:headersize + num_glyphs * bytes_per_glyph]
    result = {}

    if has_unicode:
        ut_raw = raw[headersize + num_glyphs * bytes_per_glyph:]
        glyph_idx = 0
        i = 0
        while i < len(ut_raw) and glyph_idx < num_glyphs:
            bitmap = glyph_data[glyph_idx * bytes_per_glyph:
                                 (glyph_idx + 1) * bytes_per_glyph]
            while i < len(ut_raw) and ut_raw[i] != 0xFF:
                b = ut_raw[i]
                if b < 0x80:
                    cp = b; i += 1
                elif b < 0xE0:
                    cp = ((b & 0x1F) << 6) | (ut_raw[i+1] & 0x3F); i += 2
                elif b < 0xF0:
                    cp = ((b & 0x0F) << 12) | ((ut_raw[i+1] & 0x3F) << 6) | \
                         (ut_raw[i+2] & 0x3F); i += 3
                else:
                    cp = ((b & 0x07) << 18) | ((ut_raw[i+1] & 0x3F) << 12) | \
                         ((ut_raw[i+2] & 0x3F) << 6) | (ut_raw[i+3] & 0x3F)
                    i += 4
                if b != 0xFE and cp not in result:
                    result[cp] = bitmap
            i += 1  # skip 0xFF terminator
            glyph_idx += 1

    return result


def find_fonts():
    """Return a list of PSF font paths, VGA16 fonts first."""
    paths = []
    for d in SEARCH_PATHS:
        if not os.path.isdir(d):
            continue
        # Prefer VGA16 fonts (most faithful to the original VGA ROM)
        for pat in ['*VGA16*.psf.gz', '*VGA16*.psf',
                    '*.psf.gz',       '*.psf']:
            for p in sorted(glob.glob(os.path.join(d, pat))):
                if p not in paths:
                    paths.append(p)
    return paths


def main():
    fonts = find_fonts()
    if not fonts:
        print("# ERROR: no PSF fonts found.", file=sys.stderr)
        print("# Install kbd:  sudo apt install kbd", file=sys.stderr)
        sys.exit(1)

    # Merge all fonts into one unicode->bitmap dict (first found wins)
    unicode_bitmaps = dict(FALLBACK_BITMAPS)
    loaded = 0
    for path in fonts:
        bitmaps = load_psf1(path) or load_psf2(path)
        if bitmaps:
            for cp, bmp in bitmaps.items():
                if cp not in unicode_bitmaps and len(bmp) == 16:
                    unicode_bitmaps[cp] = bmp
            loaded += 1

    print(f"# Loaded {loaded} PSF font(s), "
          f"{len(unicode_bitmaps)} unique glyphs", file=sys.stderr)

    # Build the 256-entry CP437 table
    cp437_bitmaps = []
    missing = []
    for byte_val in range(256):
        ucp = CP437_TO_UNICODE[byte_val]
        bmp = unicode_bitmaps.get(ucp)
        if bmp is None:
            missing.append((byte_val, ucp))
            bmp = bytes(16)  # blank fallback
        cp437_bitmaps.append(bmp)

    if missing:
        print(f"# Warning: {len(missing)} CP437 glyphs not found "
              f"(using blank):", file=sys.stderr)
        for bv, ucp in missing:
            print(f"#   CP437 0x{bv:02X} = U+{ucp:04X}", file=sys.stderr)

    # Emit the C header
    print("/* font_vga.h  --  VGA 8x16 CP437 font bitmap table")
    print(" *")
    print(" * Auto-generated by mkfont.py from system PSF console fonts.")
    print(" * The source fonts are derived from the IBM VGA ROM and are")
    print(" * distributed as part of the Linux kbd/console-setup project.")
    print(" *")
    print(" * Regenerate:")
    print(" *   python3 mkfont.py > font_vga.h")
    print(" *")
    print(" * Each row is 8 pixels wide; MSB is the leftmost pixel.")
    print(" * Index with:  vga_font_8x16[cp437_byte][scanline_row]")
    print(" */")
    print()
    print("#ifndef FONT_VGA_H")
    print("#define FONT_VGA_H")
    print()
    print("/* 256 x 16 bytes = 4096 bytes total */")
    print("static const unsigned char vga_font_8x16[256][16] = {")
    for i, bmp in enumerate(cp437_bitmaps):
        ucp = CP437_TO_UNICODE[i]
        hex_bytes = ', '.join(f'0x{b:02X}' for b in bmp)
        print(f"    {{ {hex_bytes} }},  /* 0x{i:02X} U+{ucp:04X} */")
    print("};")
    print()
    print("#endif /* FONT_VGA_H */")


if __name__ == '__main__':
    main()
