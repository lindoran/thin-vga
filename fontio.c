/* fontio.c  --  font file I/O for thin-vga 8x16 CP437 bitmap fonts     */

#define _POSIX_C_SOURCE 199309L

#include "fontio.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Internal constants
 * ----------------------------------------------------------------------- */

#define FONT_GLYPHS  256
#define GLYPH_H      16
#define GLYPH_W      8
#define RAW_SIZE     (FONT_GLYPHS * GLYPH_H)   /* 4096 */

/* PSF1 */
#define PSF1_MAGIC0  0x36
#define PSF1_MAGIC1  0x04
#define PSF1_MODE512 0x01   /* flag: 512 glyphs instead of 256 */
#define PSF1_HDR_SZ  4

/* PSF2 */
#define PSF2_MAGIC0  0x72
#define PSF2_MAGIC1  0xb5
#define PSF2_MAGIC2  0x4a
#define PSF2_MAGIC3  0x86
#define PSF2_HDR_SZ  32

/* -----------------------------------------------------------------------
 * Error strings
 * ----------------------------------------------------------------------- */

static const char * const err_tab[] = {
    "ok",
    "cannot open file",
    "read error / unexpected EOF",
    "write error",
    "unrecognised file format",
    "font dimensions are not 8 wide x 16 tall",
    "file contains fewer than 256 glyphs"
};

const char *fontio_strerror(int err)
{
    int i = -err;
    if (i < 0 || i >= (int)(sizeof(err_tab) / sizeof(err_tab[0])))
        return "unknown error";
    return err_tab[i];
}

/* -----------------------------------------------------------------------
 * Format detection
 * ----------------------------------------------------------------------- */

FontioFmt fontio_detect(const char *path)
{
    unsigned char hdr[4] = {0, 0, 0, 0};
    FILE *f = fopen(path, "rb");
    if (!f) return FONTIO_FMT_UNKNOWN;
    if (fread(hdr, 1, 4, f) < 4) { fclose(f); return FONTIO_FMT_UNKNOWN; }
    fclose(f);

    if (hdr[0] == PSF2_MAGIC0 && hdr[1] == PSF2_MAGIC1 &&
        hdr[2] == PSF2_MAGIC2 && hdr[3] == PSF2_MAGIC3)
        return FONTIO_FMT_PSF2;

    if (hdr[0] == PSF1_MAGIC0 && hdr[1] == PSF1_MAGIC1)
        return FONTIO_FMT_PSF1;

    return FONTIO_FMT_RAW;
}

/* -----------------------------------------------------------------------
 * Load: raw
 * ----------------------------------------------------------------------- */

int fontio_load_raw(const char *path, uint8_t buf[256][16])
{
    FILE  *f;
    size_t n;

    f = fopen(path, "rb");
    if (!f) return FONTIO_ERR_OPEN;

    n = fread(buf, 1, RAW_SIZE, f);
    fclose(f);

    return (n == RAW_SIZE) ? FONTIO_OK : FONTIO_ERR_READ;
}

/* -----------------------------------------------------------------------
 * Load: PSF1
 * ----------------------------------------------------------------------- */

int fontio_load_psf1(const char *path, uint8_t buf[256][16])
{
    unsigned char hdr[PSF1_HDR_SZ];
    uint8_t       charsize;
    int           g;
    FILE         *f;

    f = fopen(path, "rb");
    if (!f) return FONTIO_ERR_OPEN;

    if (fread(hdr, 1, PSF1_HDR_SZ, f) != PSF1_HDR_SZ) {
        fclose(f); return FONTIO_ERR_READ;
    }
    if (hdr[0] != PSF1_MAGIC0 || hdr[1] != PSF1_MAGIC1) {
        fclose(f); return FONTIO_ERR_FORMAT;
    }

    charsize = hdr[3];
    if (charsize != GLYPH_H) { fclose(f); return FONTIO_ERR_SIZE; }

    /* mode byte: bit 0 => 512 glyphs, but we only need the first 256  */
    for (g = 0; g < FONT_GLYPHS; g++) {
        if (fread(buf[g], 1, GLYPH_H, f) != (size_t)GLYPH_H) {
            fclose(f); return FONTIO_ERR_READ;
        }
    }

    fclose(f);
    return FONTIO_OK;
}

/* -----------------------------------------------------------------------
 * Load: PSF2
 *
 * Header layout (all fields little-endian uint32):
 *   [0-3]   magic
 *   [4-7]   version  (must be 0)
 *   [8-11]  headersize
 *   [12-15] flags    (bit 0 = has unicode table, ignored here)
 *   [16-19] numglyph
 *   [20-23] bytesperglyph
 *   [24-27] height
 *   [28-31] width
 * ----------------------------------------------------------------------- */

static uint32_t u32le(const unsigned char *p)
{
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fontio_load_psf2(const char *path, uint8_t buf[256][16])
{
    unsigned char hdr[PSF2_HDR_SZ];
    uint32_t      hdrsize, numglyph, bpg, height, width;
    int           g;
    FILE         *f;

    f = fopen(path, "rb");
    if (!f) return FONTIO_ERR_OPEN;

    if (fread(hdr, 1, PSF2_HDR_SZ, f) != PSF2_HDR_SZ) {
        fclose(f); return FONTIO_ERR_READ;
    }
    if (hdr[0] != PSF2_MAGIC0 || hdr[1] != PSF2_MAGIC1 ||
        hdr[2] != PSF2_MAGIC2 || hdr[3] != PSF2_MAGIC3) {
        fclose(f); return FONTIO_ERR_FORMAT;
    }

    hdrsize  = u32le(hdr + 8);
    numglyph = u32le(hdr + 16);
    bpg      = u32le(hdr + 20);
    height   = u32le(hdr + 24);
    width    = u32le(hdr + 28);

    if (width != GLYPH_W || height != GLYPH_H || bpg != GLYPH_H) {
        fclose(f); return FONTIO_ERR_SIZE;
    }
    if (numglyph < FONT_GLYPHS) {
        fclose(f); return FONTIO_ERR_GLYPHS;
    }

    /* seek past any extra header bytes (hdrsize may be > 32) */
    if (fseek(f, (long)hdrsize, SEEK_SET) != 0) {
        fclose(f); return FONTIO_ERR_READ;
    }

    for (g = 0; g < FONT_GLYPHS; g++) {
        if (fread(buf[g], 1, GLYPH_H, f) != (size_t)GLYPH_H) {
            fclose(f); return FONTIO_ERR_READ;
        }
    }

    fclose(f);
    return FONTIO_OK;
}

/* -----------------------------------------------------------------------
 * Load: PSF (auto-detect v1 vs v2)
 * ----------------------------------------------------------------------- */

int fontio_load_psf(const char *path, uint8_t buf[256][16])
{
    FontioFmt fmt = fontio_detect(path);
    if (fmt == FONTIO_FMT_PSF1) return fontio_load_psf1(path, buf);
    if (fmt == FONTIO_FMT_PSF2) return fontio_load_psf2(path, buf);
    return FONTIO_ERR_FORMAT;
}

/* -----------------------------------------------------------------------
 * Load: auto-detect
 * ----------------------------------------------------------------------- */

int fontio_load(const char *path, uint8_t buf[256][16])
{
    switch (fontio_detect(path)) {
    case FONTIO_FMT_PSF1: return fontio_load_psf1(path, buf);
    case FONTIO_FMT_PSF2: return fontio_load_psf2(path, buf);
    default:              return fontio_load_raw (path, buf);
    }
}

/* -----------------------------------------------------------------------
 * Save: raw
 * ----------------------------------------------------------------------- */

int fontio_save_raw(const char *path, const uint8_t font[256][16])
{
    FILE *f = fopen(path, "wb");
    if (!f) return FONTIO_ERR_OPEN;
    if (fwrite(font, 1, RAW_SIZE, f) != RAW_SIZE) {
        fclose(f); return FONTIO_ERR_WRITE;
    }
    fclose(f);
    return FONTIO_OK;
}

/* -----------------------------------------------------------------------
 * Save: PSF1
 * ----------------------------------------------------------------------- */

int fontio_save_psf1(const char *path, const uint8_t font[256][16])
{
    unsigned char hdr[PSF1_HDR_SZ];
    FILE *f;

    f = fopen(path, "wb");
    if (!f) return FONTIO_ERR_OPEN;

    hdr[0] = PSF1_MAGIC0;
    hdr[1] = PSF1_MAGIC1;
    hdr[2] = 0;        /* mode: 256 glyphs, no unicode table */
    hdr[3] = GLYPH_H;  /* charsize                           */

    if (fwrite(hdr,  1, PSF1_HDR_SZ, f) != PSF1_HDR_SZ ||
        fwrite(font, 1, RAW_SIZE,    f) != RAW_SIZE) {
        fclose(f); return FONTIO_ERR_WRITE;
    }

    fclose(f);
    return FONTIO_OK;
}

/* -----------------------------------------------------------------------
 * Save: PSF2
 * ----------------------------------------------------------------------- */

int fontio_save_psf2(const char *path, const uint8_t font[256][16])
{
    unsigned char hdr[PSF2_HDR_SZ];
    FILE *f;

    f = fopen(path, "wb");
    if (!f) return FONTIO_ERR_OPEN;

    memset(hdr, 0, PSF2_HDR_SZ);
    hdr[0]  = PSF2_MAGIC0;  hdr[1]  = PSF2_MAGIC1;
    hdr[2]  = PSF2_MAGIC2;  hdr[3]  = PSF2_MAGIC3;
    /* version   = 0                                  (bytes 4-7,  zeroed) */
    /* headersize = 32 */
    hdr[8]  = PSF2_HDR_SZ;
    /* flags     = 0 (no unicode table)               (bytes 12-15, zeroed) */
    /* numglyph  = 256 = 0x00000100 */
    hdr[17] = 1;
    /* bytesperglyph = 16 */
    hdr[20] = GLYPH_H;
    /* height        = 16 */
    hdr[24] = GLYPH_H;
    /* width         = 8  */
    hdr[28] = GLYPH_W;

    if (fwrite(hdr,  1, PSF2_HDR_SZ, f) != PSF2_HDR_SZ ||
        fwrite(font, 1, RAW_SIZE,    f) != RAW_SIZE) {
        fclose(f); return FONTIO_ERR_WRITE;
    }

    fclose(f);
    return FONTIO_OK;
}

/* -----------------------------------------------------------------------
 * Save: C header
 * ----------------------------------------------------------------------- */

/* Build a valid C include-guard token from an arbitrary symbol name.    */
static void make_guard(char *out, size_t sz, const char *sym)
{
    size_t i;
    snprintf(out, sz, "FONT_%s_H", sym);
    for (i = 0; out[i] && i < sz - 1; i++) {
        char c = out[i];
        if      (c >= 'a' && c <= 'z') out[i] = (char)(c - 32);
        else if (!((c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') ||
                    c == '_'))          out[i] = '_';
    }
}

int fontio_save_h(const char *path, const uint8_t font[256][16],
                  const char *sym)
{
    char  guard[128];
    int   i, row;
    FILE *f;

    if (!sym || !sym[0]) sym = "vga_font_8x16";
    make_guard(guard, sizeof(guard), sym);

    f = fopen(path, "w");
    if (!f) return FONTIO_ERR_OPEN;

    fprintf(f,
        "/* %s -- VGA 8x16 CP437 font bitmap\n"
        " * Generated by fontio.\n"
        " * Index: %s[cp437_byte][scanline_row]\n"
        " * MSB of each byte is the leftmost pixel.\n"
        " */\n\n"
        "#ifndef %s\n"
        "#define %s\n\n"
        "static const unsigned char %s[256][16] = {\n",
        path, sym, guard, guard, sym
    );

    for (i = 0; i < FONT_GLYPHS; i++) {
        fputs("    { ", f);
        for (row = 0; row < GLYPH_H; row++) {
            fprintf(f, "0x%02X", font[i][row]);
            if (row < GLYPH_H - 1) fputs(", ", f);
        }
        fputs(" }", f);
        if (i < FONT_GLYPHS - 1) fputc(',', f);
        if (i >= 0x20 && i < 0x7F)
            fprintf(f, "  /* 0x%02X '%c' */\n", i, i);
        else
            fprintf(f, "  /* 0x%02X */\n", i);
    }

    fprintf(f, "};\n\n#endif /* %s */\n", guard);
    fclose(f);
    return FONTIO_OK;
}
