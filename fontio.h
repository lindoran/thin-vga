/* fontio.h  --  font file I/O for thin-vga 8x16 CP437 bitmap fonts
 *
 * Supported formats (auto-detected on load):
 *
 *   RAW    -- 4096-byte flat binary: 256 glyphs × 16 bytes, no header.
 *             Easy to mmap or fread directly into a font slot.
 *
 *   PSF1   -- PC Screen Font v1 (4-byte header + raw glyph data).
 *             The traditional Linux console font format; widely available.
 *
 *   PSF2   -- PC Screen Font v2 (32-byte header + glyph data).
 *             Modern variant; also widely available.  Unicode tables are
 *             silently ignored on load and not written on save.
 *
 *   H      -- C header.  Produces or reads a drop-in replacement for
 *             font_vga.h.
 *
 * All load functions fill exactly buf[256][16].  If a PSF file contains
 * more than 256 glyphs, only the first 256 are loaded.
 *
 * Disk I/O and buffer management are entirely the caller's responsibility.
 * vgaterm_set_font_slot() is the primitive for loading a buffer into the
 * renderer; fontio only handles the bytes-on-disk side.
 */

#ifndef FONTIO_H
#define FONTIO_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Error codes
 * ----------------------------------------------------------------------- */

#define FONTIO_OK          0
#define FONTIO_ERR_OPEN   -1   /* could not open file                    */
#define FONTIO_ERR_READ   -2   /* unexpected EOF or read failure          */
#define FONTIO_ERR_WRITE  -3   /* write failure                          */
#define FONTIO_ERR_FORMAT -4   /* magic bytes wrong / unrecognised format */
#define FONTIO_ERR_SIZE   -5   /* font is not 8 pixels wide × 16 tall    */
#define FONTIO_ERR_GLYPHS -6   /* fewer than 256 glyphs in file          */

const char *fontio_strerror(int err);

/* -----------------------------------------------------------------------
 * Format identification
 * ----------------------------------------------------------------------- */

typedef enum {
    FONTIO_FMT_UNKNOWN = 0,
    FONTIO_FMT_RAW,
    FONTIO_FMT_PSF1,
    FONTIO_FMT_PSF2,
    FONTIO_FMT_H
} FontioFmt;

/* Peek at the file header and return the detected format.
 * Returns FONTIO_FMT_UNKNOWN if the file cannot be opened.
 * RAW is the fallback when no recognised magic bytes are present.       */
FontioFmt fontio_detect(const char *path);

/* -----------------------------------------------------------------------
 * Load  (fill buf[256][16] from file)
 * ----------------------------------------------------------------------- */

/* Auto-detect format and load.  Recommended for most callers.           */
int fontio_load    (const char *path, uint8_t buf[256][16]);

/* Explicit format loaders -- useful when you know what you have.        */
int fontio_load_raw (const char *path, uint8_t buf[256][16]);
int fontio_load_psf1(const char *path, uint8_t buf[256][16]);
int fontio_load_psf2(const char *path, uint8_t buf[256][16]);
int fontio_load_h   (const char *path, uint8_t buf[256][16]);

/* Accepts either PSF variant; auto-detects which one.                   */
int fontio_load_psf (const char *path, uint8_t buf[256][16]);

/* -----------------------------------------------------------------------
 * Save
 * ----------------------------------------------------------------------- */

/* 4096-byte flat binary -- fastest for runtime font swapping.           */
int fontio_save_raw (const char *path, const uint8_t font[256][16]);

/* PSF1 -- 4-byte header + 4096 bytes.  Compatible with setfont(8).     */
int fontio_save_psf1(const char *path, const uint8_t font[256][16]);

/* PSF2 -- 32-byte header + 4096 bytes.  No unicode table written.      */
int fontio_save_psf2(const char *path, const uint8_t font[256][16]);

/* C header -- drop-in replacement for font_vga.h.
 * sym is the C identifier for the array (e.g. "vga_font_8x16").
 * Pass NULL to use "vga_font_8x16" as the default.                      */
int fontio_save_h   (const char *path, const uint8_t font[256][16],
                     const char *sym);

#endif /* FONTIO_H */
