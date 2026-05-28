/* SPDX-License-Identifier: MIT
 * fontedit.c  --  8x16 VGA bitmap font editor for thin-vga
 *
 * Build:
 *   cc -o fontedit fontedit.c vgaterm.c vio.c -lX11 -lm
 *
 * Layout (80x25 VGA text screen):
 *
 *   Col  0-17   Pixel editor box  (8px wide x 16px tall, 2-chars/pixel)
 *   Col 19-52   Character map     (32 cols x 8 rows = 256 glyphs)
 *   Col 54-79   Info / hex dump   (glyph code, preview, row bytes)
 *   Row  0      Title bar
 *   Row 24      Status / key hints
 *
 * Keys:
 *   Tab              switch focus: pixel editor <-> character map
 *   Arrows           move cursor in focused panel
 *   Space / Enter    toggle pixel (editor)  /  select glyph (char map)
 *   [ / ]            previous / next glyph  (both panels)
 *   Ctrl+S / F2      save font to font_vga.h
 *   Ctrl+Z           undo last pixel change
 *   F1               toggle help bar
 *   Ctrl+Q / F10     quit
 *
 * Usage:
 *   ./fontedit [font.raw|font.psf|font.h]
 */

#define _POSIX_C_SOURCE 199309L

#include "vgaterm.h"
#include "vio.h"
#include "font_vga.h"
#include "fontio.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Font working copy
 * ----------------------------------------------------------------------- */

#define DLG_FIELD_MAX  60               /* max save-as filename length   */

static uint8_t font[256][16];
static int     dirty = 0;
static char    save_filename[DLG_FIELD_MAX + 1] = "font_vga.h";

/* Global terminal handle -- set in main() so draw helpers can reach fplane. */
static VGATerm *vt_g = NULL;

/* Mark a rectangle of cells to render from font B (the working font). */
static void fp_set(int col, int row, int w, int h)
{
    vgaterm_set_fplane_rect(vt_g, col, row, w, h, 1);
}

static void set_save_filename(const char *path)
{
    if (!path || !path[0]) return;

    strncpy(save_filename, path, DLG_FIELD_MAX);
    save_filename[DLG_FIELD_MAX] = '\0';
}

/* Simple single-level undo: save the previous state of one glyph row. */
static struct { int valid; int g; int y; uint8_t before; } undo_buf;

/* -----------------------------------------------------------------------
 * Layout
 * ----------------------------------------------------------------------- */

/* Pixel editor box: cols 0-17, rows 1-18 */
#define ED_BX    0      /* box left   */
#define ED_BY    1      /* box top    */
#define ED_BW    18     /* box width  (16 content + 2 border) */
#define ED_BH    18     /* box height (16 content + 2 border) */
#define ED_IX    1      /* interior left  */
#define ED_IY    2      /* interior top   */

/* Character map box: cols 19-52, rows 1-10 */
#define CM_BX    19
#define CM_BY    1
#define CM_BW    34     /* 32 content + 2 border */
#define CM_BH    10     /*  8 content + 2 border */
#define CM_IX    20     /* interior left  */
#define CM_IY    2      /* interior top   */
#define CM_COLS  32
#define CM_ROWS  8

/* Info panel: cols 54-79, rows 1-23 */
#define INF_X    54
#define INF_Y    1
#define INF_W    26

/* Save-as dialog: 54 wide x 7 tall, centred */
#define DLG_COL        13
#define DLG_ROW        9
#define DLG_W          54
#define DLG_H          7
#define DLG_FIELD_COL  (DLG_COL + 12)   /* col 25 -- after "  Filename: " (10 chars at col+2) */
#define DLG_FIELD_ROW  (DLG_ROW + 2)    /* row 11 -- same row as label */
#define DLG_FIELD_W    (DLG_W - 16)     /* 38 visible chars in field     */

/* -----------------------------------------------------------------------
 * Color scheme
 * ----------------------------------------------------------------------- */

/* Structural */
#define A_NORMAL     VGA_ATTR(VGA_BLACK, VGA_LGRAY)
#define A_TITLE      VGA_ATTR(VGA_BLUE,  VGA_WHITE)
#define A_STATUS     VGA_ATTR(VGA_CYAN,  VGA_BLACK)
#define A_BORDER     VGA_ATTR(VGA_BLACK, VGA_LGRAY)
#define A_BORDER_ACT VGA_ATTR(VGA_BLUE,  VGA_WHITE)

/* Pixel editor */
#define A_PIX_ON     VGA_ATTR(VGA_BLUE,  VGA_WHITE)
#define A_PIX_OFF    VGA_ATTR(VGA_DGRAY, VGA_BLACK)
#define A_CUR_ON     VGA_ATTR(VGA_RED,   VGA_YELLOW)
#define A_CUR_OFF    VGA_ATTR(VGA_RED,   VGA_LGRAY)

/* Character map */
#define A_CM_NORM    VGA_ATTR(VGA_BLACK, VGA_LGRAY)
#define A_CM_SEL     VGA_ATTR(VGA_BLUE,  VGA_WHITE)
#define A_CM_CUR     VGA_ATTR(VGA_BLACK, VGA_YELLOW)
#define A_CM_CURSEL  VGA_ATTR(VGA_RED,   VGA_YELLOW)

/* Info panel */
#define A_INFO       VGA_ATTR(VGA_BLACK, VGA_LGRAY)
#define A_INFO_HEAD  VGA_ATTR(VGA_DGRAY, VGA_LGRAY)
#define A_HEX_ON     VGA_ATTR(VGA_BLUE,  VGA_LGRAY)
#define A_HEX_OFF    VGA_ATTR(VGA_DGRAY, VGA_BLACK)

/* -----------------------------------------------------------------------
 * State
 * ----------------------------------------------------------------------- */

#define PANEL_EDITOR  0
#define PANEL_CHARMAP 1

static int panel     = PANEL_EDITOR;
static int glyph     = 0x41;   /* current glyph (default: 'A') */
static int px        = 0;      /* pixel cursor x, 0-7  */
static int py        = 0;      /* pixel cursor y, 0-15 */
static int mx        = 0;      /* charmap cursor x, 0-31 */
static int my        = 0;      /* charmap cursor y, 0-7  */
static int show_help = 1;

/* -----------------------------------------------------------------------
 * Pixel helpers
 * ----------------------------------------------------------------------- */

static int get_pixel(int g, int x, int y)
{
    return (font[g][y] >> (7 - x)) & 1;
}

static void set_pixel(int g, int x, int y, int v)
{
    /* save undo state before first change on this row */
    if (!undo_buf.valid || undo_buf.g != g || undo_buf.y != y) {
        undo_buf.valid  = 1;
        undo_buf.g      = g;
        undo_buf.y      = y;
        undo_buf.before = font[g][y];
    }
    if (v)
        font[g][y] |=  (uint8_t)(1u << (7 - x));
    else
        font[g][y] &= ~(uint8_t)(1u << (7 - x));
    dirty = 1;
}

static void toggle_pixel(int g, int x, int y)
{
    set_pixel(g, x, y, !get_pixel(g, x, y));
}

static void do_undo(void)
{
    if (!undo_buf.valid) return;
    font[undo_buf.g][undo_buf.y] = undo_buf.before;
    undo_buf.valid = 0;
    dirty = 1;
}

/* -----------------------------------------------------------------------
 * Title bar
 * ----------------------------------------------------------------------- */

static void draw_titlebar(void)
{
    char buf[81];
    vio_fill(0, 0, VGA_COLS, 1, ' ', A_TITLE);
    vio_gotoxy(2, 0);
    vio_setattr(A_TITLE);
    snprintf(buf, sizeof(buf), "VGA Font Editor%s", dirty ? " [modified]" : "");
    vio_puts(buf);
    vio_gotoxy(32, 0);
    if (glyph >= 0x20 && glyph < 0x7F)
        snprintf(buf, sizeof(buf), "Glyph 0x%02X '%c'", glyph, glyph);
    else
        snprintf(buf, sizeof(buf), "Glyph 0x%02X", glyph);
    vio_puts(buf);
}

/* -----------------------------------------------------------------------
 * Status bar
 * ----------------------------------------------------------------------- */

static void draw_statusbar(void)
{
    vio_fill(0, 24, VGA_COLS, 1, ' ', A_STATUS);
    vio_gotoxy(1, 24);
    vio_setattr(A_STATUS);
    if (show_help)
        vio_puts("Tab:Panel  Spc:Toggle  []:Glyph  ^Z:Undo  ^S:Save  ^Q:Quit");
    else
        vio_puts("F1:Help");
}

/* -----------------------------------------------------------------------
 * Pixel editor panel
 * ----------------------------------------------------------------------- */

static void draw_pixel_editor(void)
{
    int x, y;
    char label[20];
    uint8_t battr = (panel == PANEL_EDITOR) ? A_BORDER_ACT : A_BORDER;

    vio_dbox(ED_BX, ED_BY, ED_BW, ED_BH, battr);

    /* label in top border */
    snprintf(label, sizeof(label), " 0x%02X ", glyph);
    vio_gotoxy(ED_BX + 2, ED_BY);
    vio_setattr(battr);
    vio_puts(label);

    /* row numbers on left side if room - no room in 1 char border; skip */

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 8; x++) {
            int  on      = get_pixel(glyph, x, y);
            int  is_cur  = (panel == PANEL_EDITOR && x == px && y == py);
            uint8_t attr = is_cur ? (on ? A_CUR_ON : A_CUR_OFF)
                                  : (on ? A_PIX_ON : A_PIX_OFF);
            uint8_t ch   = on ? 0xDB : ' ';   /* CP437 full block or space */

            vio_putch_at(ED_IX + x * 2,     ED_IY + y, ch, attr);
            vio_putch_at(ED_IX + x * 2 + 1, ED_IY + y, ch, attr);
        }
    }
}

/* -----------------------------------------------------------------------
 * Character map panel
 * ----------------------------------------------------------------------- */

static void draw_charmap(void)
{
    int i, cx, cy;
    uint8_t battr = (panel == PANEL_CHARMAP) ? A_BORDER_ACT : A_BORDER;

    vio_dbox(CM_BX, CM_BY, CM_BW, CM_BH, battr);
    vio_gotoxy(CM_BX + 2, CM_BY);
    vio_setattr(battr);
    vio_puts(" Character Map ");

    for (i = 0; i < 256; i++) {
        uint8_t attr;
        int is_glyph  = (i == glyph);
        int is_cursor = (panel == PANEL_CHARMAP &&
                         (i % CM_COLS) == mx && (i / CM_COLS) == my);
        cx = CM_IX + (i % CM_COLS);
        cy = CM_IY + (i / CM_COLS);

        if (is_cursor && is_glyph)  attr = A_CM_CURSEL;
        else if (is_cursor)         attr = A_CM_CUR;
        else if (is_glyph)          attr = A_CM_SEL;
        else                        attr = A_CM_NORM;

        vio_putch_at(cx, cy, (uint8_t)i, attr);
    }
    /* charmap shows glyphs from the working font */
    fp_set(CM_IX, CM_IY, CM_COLS, CM_ROWS);
}

/* -----------------------------------------------------------------------
 * Shortcut reference box (cols 19-52, rows 11-18 -- below charmap)
 * ----------------------------------------------------------------------- */

static void draw_tools_panel(void)
{
    /* box sits directly under the charmap and bottoms-out at row 18,
     * same as the pixel editor -- so both columns look level.          */
    static const char * const lines[] = {
        "Arrows  Move cursor",
        "Spc     Toggle pixel",
        "c Clear     i Invert",
        "h H-flip    v V-flip",
        "0/1  Force clr / set",
        "n / p   Prev/next glyph"
    };
    int i;

    vio_dbox(CM_BX, 11, CM_BW, 8, A_BORDER);
    vio_gotoxy(CM_BX + 2, 11);
    vio_setattr(A_BORDER);
    vio_puts(" Keys ");

    vio_setattr(A_NORMAL);
    for (i = 0; i < 6; i++) {
        vio_gotoxy(CM_BX + 2, 12 + i);
        vio_puts(lines[i]);
    }
}

static void draw_info(void)
{
    int row, bit;
    char buf[32];
    int col = INF_X;

    vio_fill(col, INF_Y, INF_W, 23, ' ', A_INFO);

    /* Glyph identification */
    vio_setattr(A_INFO_HEAD);
    vio_gotoxy(col, 1); vio_puts("Glyph");
    vio_setattr(A_INFO);
    vio_gotoxy(col, 2);
    if (glyph >= 0x20 && glyph < 0x7F)
        snprintf(buf, sizeof(buf), "0x%02X  '%c'", glyph, glyph);
    else
        snprintf(buf, sizeof(buf), "0x%02X  ctrl/ext", glyph);
    vio_puts(buf);

    /* Actual-size preview at three attribute combos */
    vio_setattr(A_INFO_HEAD);
    vio_gotoxy(col, 4); vio_puts("Preview");
    vio_putch_at(col,     5, (uint8_t)glyph, VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vio_putch_at(col + 2, 5, (uint8_t)glyph, VGA_ATTR(VGA_BLACK, VGA_LGRAY));
    vio_putch_at(col + 4, 5, (uint8_t)glyph, VGA_ATTR(VGA_WHITE, VGA_BLACK));
    vio_putch_at(col + 6, 5, (uint8_t)glyph, VGA_ATTR(VGA_BLUE,  VGA_YELLOW));
    /* preview chars render from the working font */
    fp_set(col, 5, 7, 1);

    /* Pixel cursor position */
    vio_setattr(A_INFO);
    vio_gotoxy(col, 6);
    snprintf(buf, sizeof(buf), "px=%d py=%d", px, py);
    vio_puts(buf);

    /* Hex dump with bit visualisation */
    vio_setattr(A_INFO_HEAD);
    vio_gotoxy(col, 7);
    vio_puts("# Hex 76543210");

    for (row = 0; row < 16; row++) {
        uint8_t byte = font[glyph][row];
        int is_cur_row = (panel == PANEL_EDITOR && row == py);
        uint8_t row_attr = is_cur_row
                         ? VGA_ATTR(VGA_BLUE, VGA_WHITE)
                         : A_INFO;

        vio_gotoxy(col, 8 + row);
        vio_setattr(row_attr);
        snprintf(buf, sizeof(buf), "%X %02X ", row, byte);
        vio_puts(buf);

        /* 8 individual bits */
        for (bit = 7; bit >= 0; bit--) {
            int on = (byte >> bit) & 1;
            int is_px = (panel == PANEL_EDITOR && row == py && (7 - bit) == px);
            uint8_t battr = is_px ? A_CUR_ON
                          : (on   ? A_HEX_ON : A_HEX_OFF);
            uint8_t ch    = on ? 0xFE : 0xFA;   /* CP437: small sq / dot */
            vio_putch_at(col + 5 + (7 - bit), 8 + row, ch, battr);
        }
    }
}

/* -----------------------------------------------------------------------
 * Full redraw
 * ----------------------------------------------------------------------- */

static void draw_all(void)
{
    vio_setattr(A_NORMAL);
    /* reset every cell to font A; individual draw functions will mark
     * their preview/charmap regions as font B after writing them.      */
    memset(vgaterm_fplane(vt_g), 0, VGA_ROWS * VGA_COLS);
    vio_clrscr();
    draw_titlebar();
    draw_pixel_editor();
    draw_charmap();
    draw_tools_panel();
    draw_info();
    draw_statusbar();
}

/* -----------------------------------------------------------------------
 * Save font_vga.h
 * ----------------------------------------------------------------------- */

static void save_font(const char *filename)
{
    int err = fontio_save_h(filename, (const uint8_t (*)[16])font, "vga_font_8x16");
    if (err != FONTIO_OK) {
        vio_fill(0, 12, VGA_COLS, 1, ' ', VGA_ATTR(VGA_RED, VGA_WHITE));
        vio_gotoxy(2, 12);
        vio_setattr(VGA_ATTR(VGA_RED, VGA_WHITE));
        vio_puts(fontio_strerror(err));
        vio_flush();
        return;
    }
    dirty = 0;
}

/* -----------------------------------------------------------------------
 * Save-as dialog
 * ----------------------------------------------------------------------- */

#define A_DLG        VGA_ATTR(VGA_BLACK, VGA_LGRAY)
#define A_DLG_FRAME  VGA_ATTR(VGA_BLUE,  VGA_WHITE)
#define A_DLG_FIELD  VGA_ATTR(VGA_BLACK, VGA_CYAN)
#define A_DLG_CUR    VGA_ATTR(VGA_CYAN,  VGA_BLACK)
#define A_DLG_HINT   VGA_ATTR(VGA_DGRAY, VGA_LGRAY)

static void draw_save_dialog(int cur)
{
    int flen, fstart, i, tlen, tcol;
    const char *title = " Save Font As ";

    /* frame */
    vio_dbox(DLG_COL, DLG_ROW, DLG_W, DLG_H, A_DLG_FRAME);

    /* title in top border */
    tlen = (int)strlen(title);
    tcol = DLG_COL + (DLG_W - tlen) / 2;
    vio_gotoxy(tcol, DLG_ROW);
    vio_setattr(A_DLG_FRAME);
    vio_puts(title);

    /* clear interior */
    vio_fill(DLG_COL + 1, DLG_ROW + 1, DLG_W - 2, DLG_H - 2, ' ', A_DLG);

    /* "Filename: " label -- same row as the field */
    vio_gotoxy(DLG_COL + 2, DLG_FIELD_ROW);
    vio_setattr(A_DLG);
    vio_puts("Filename: ");

    /* input field background */
    vio_fill(DLG_FIELD_COL, DLG_FIELD_ROW, DLG_FIELD_W, 1, ' ', A_DLG_FIELD);

    /* text, scrolled so cursor is always visible */
    flen   = (int)strlen(save_filename);
    fstart = (cur >= DLG_FIELD_W) ? cur - DLG_FIELD_W + 1 : 0;

    for (i = 0; i < DLG_FIELD_W; i++) {
        int pos = fstart + i;
        uint8_t ch   = (pos < flen) ? (uint8_t)save_filename[pos] : ' ';
        uint8_t attr = (pos == cur) ? A_DLG_CUR : A_DLG_FIELD;
        vio_putch_at(DLG_FIELD_COL + i, DLG_FIELD_ROW, ch, attr);
    }
    /* cursor block when at end-of-string */
    if (cur == flen && (cur - fstart) < DLG_FIELD_W)
        vio_putch_at(DLG_FIELD_COL + (cur - fstart), DLG_FIELD_ROW,
                     '_', A_DLG_CUR);

    /* hint line */
    vio_fill(DLG_COL + 1, DLG_ROW + 4, DLG_W - 2, 1, ' ', A_DLG_HINT);
    vio_gotoxy(DLG_COL + 2, DLG_ROW + 4);
    vio_setattr(A_DLG_HINT);
    vio_puts("Enter: Save        Esc: Cancel");
}

/* Returns 1 if user confirmed (save_filename is set), 0 if cancelled. */
static int run_save_dialog(void)
{
    int cur = (int)strlen(save_filename);
    int k, len;

    draw_save_dialog(cur);
    vio_flush();

    for (;;) {
        k = vio_getch();
        if (k == KEY_CLOSED) return 0;

        len = (int)strlen(save_filename);

        switch (k) {
        case KEY_ESC:
            return 0;

        case KEY_ENTER:
            if (len > 0) return 1;
            break;

        case KEY_LEFT:
            if (cur > 0) cur--;
            break;
        case KEY_RIGHT:
            if (cur < len) cur++;
            break;
        case KEY_HOME: case KEY_CTRL_HOME:
            cur = 0;
            break;
        case KEY_END: case KEY_CTRL_END:
            cur = len;
            break;

        case KEY_BS:
            if (cur > 0) {
                memmove(save_filename + cur - 1,
                        save_filename + cur,
                        (size_t)(len - cur + 1));
                cur--;
            }
            break;
        case KEY_DEL:
            if (cur < len)
                memmove(save_filename + cur,
                        save_filename + cur + 1,
                        (size_t)(len - cur));
            break;

        default:
            /* printable ASCII insert */
            if (k >= 0x20 && k < 0x7F && len < DLG_FIELD_MAX) {
                memmove(save_filename + cur + 1,
                        save_filename + cur,
                        (size_t)(len - cur + 1));
                save_filename[cur] = (char)k;
                cur++;
            }
            break;
        }

        draw_save_dialog(cur);
        vio_flush();
    }
}

static void flash_msg(const char *msg, uint8_t attr)
{
    vio_fill(INF_X, 23, INF_W, 1, ' ', attr);
    vio_gotoxy(INF_X, 23);
    vio_setattr(attr);
    vio_puts(msg);
    vio_flush();
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int k;
    VGATerm *vt;

    /* initialise working font from compiled-in table */
    memcpy(font, vga_font_8x16, sizeof(font));
    undo_buf.valid = 0;

    if (argc > 1) {
        int err = fontio_load(argv[1], font);
        if (err != FONTIO_OK) {
            fprintf(stderr, "fontedit: %s: %s\n", argv[1], fontio_strerror(err));
            return 1;
        }
        set_save_filename(argv[1]);
    }

    /* sync charmap cursor to initial glyph */
    mx = glyph % CM_COLS;
    my = glyph / CM_COLS;

    vt = vgaterm_open("VGA Font Editor");
    if (!vt) return 1;

    vio_init(vt);
    vt_g = vt;
    /* slot 0 = NULL (built-in VGA font, used for all UI)
     * slot 1 = working font array (charmap + preview cells via fplane)  */
    vgaterm_set_font_slot(vt, 1, (const unsigned char (*)[16])font);
    vio_hide_cursor();
    draw_all();
    vio_flush();

    for (;;) {
        k = vio_getch();
        if (k == KEY_CLOSED)         break;
        if (k == KEY_CTRL('q') || k == KEY_F10) break;

        /* --- global keys --- */
        if (k == KEY_F1) {
            show_help = !show_help;
            draw_statusbar();
            vio_flush();
            continue;
        }

        if (k == KEY_F2 || k == KEY_CTRL('s')) {
            if (run_save_dialog()) {
                save_font(save_filename);
                draw_all();
                flash_msg("Saved!", VGA_ATTR(VGA_GREEN, VGA_WHITE));
            } else {
                draw_all();
            }
            vio_flush();
            continue;
        }

        if (k == KEY_CTRL('z')) {
            do_undo();
            draw_pixel_editor();
            draw_info();
            draw_titlebar();
            vio_flush();
            continue;
        }

        /* --- glyph navigation (both panels) --- */
        if (k == '[' || k == KEY_PGUP) {
            if (glyph > 0) glyph--;
            mx = glyph % CM_COLS;
            my = glyph / CM_COLS;
            undo_buf.valid = 0;
            draw_all();
            vio_flush();
            continue;
        }
        if (k == ']' || k == KEY_PGDN) {
            if (glyph < 255) glyph++;
            mx = glyph % CM_COLS;
            my = glyph / CM_COLS;
            undo_buf.valid = 0;
            draw_all();
            vio_flush();
            continue;
        }

        /* --- panel switch --- */
        if (k == KEY_TAB || k == KEY_SHIFT_TAB) {
            panel = (panel == PANEL_EDITOR) ? PANEL_CHARMAP : PANEL_EDITOR;
            draw_pixel_editor();
            draw_charmap();
            vio_flush();
            continue;
        }

        /* --- panel-specific keys --- */
        if (panel == PANEL_EDITOR) {
            switch (k) {
            case KEY_UP:
                if (py > 0) { py--; undo_buf.valid = 0; }
                break;
            case KEY_DOWN:
                if (py < 15) { py++; undo_buf.valid = 0; }
                break;
            case KEY_LEFT:
                if (px > 0) px--;
                else if (py > 0) { py--; px = 7; undo_buf.valid = 0; }
                break;
            case KEY_RIGHT:
                if (px < 7) px++;
                else if (py < 15) { py++; px = 0; undo_buf.valid = 0; }
                break;
            case KEY_HOME:  px = 0; break;
            case KEY_END:   px = 7; break;
            case KEY_CTRL_HOME: px = 0; py = 0; undo_buf.valid = 0; break;
            case KEY_CTRL_END:  px = 7; py = 15; undo_buf.valid = 0; break;
            case ' ':
            case KEY_ENTER:
                toggle_pixel(glyph, px, py);
                break;
            default:
                /* '1'/'0': force set/clear pixel */
                if (k == '1') { set_pixel(glyph, px, py, 1); break; }
                if (k == '0') { set_pixel(glyph, px, py, 0); break; }
                /* 'c': clear whole glyph */
                if (k == 'c') {
                    int r;
                    for (r = 0; r < 16; r++)
                        font[glyph][r] = 0;
                    dirty = 1; undo_buf.valid = 0;
                    break;
                }
                /* 'i': invert whole glyph */
                if (k == 'i') {
                    int r;
                    for (r = 0; r < 16; r++)
                        font[glyph][r] ^= 0xFF;
                    dirty = 1; undo_buf.valid = 0;
                    break;
                }
                /* 'h': flip glyph horizontally */
                if (k == 'h') {
                    int r, b;
                    for (r = 0; r < 16; r++) {
                        uint8_t orig = font[glyph][r], flipped = 0;
                        for (b = 0; b < 8; b++)
                            if (orig & (1u << b))
                                flipped |= (1u << (7 - b));
                        font[glyph][r] = flipped;
                    }
                    dirty = 1; undo_buf.valid = 0;
                    break;
                }
                /* 'v': flip glyph vertically */
                if (k == 'v') {
                    int top, bot;
                    for (top = 0, bot = 15; top < bot; top++, bot--) {
                        uint8_t tmp = font[glyph][top];
                        font[glyph][top] = font[glyph][bot];
                        font[glyph][bot] = tmp;
                    }
                    dirty = 1; undo_buf.valid = 0;
                    break;
                }
                /* 'n'/'p': next/prev glyph (aliases) */
                if (k == 'n' && glyph < 255) {
                    glyph++; mx = glyph % CM_COLS; my = glyph / CM_COLS;
                    undo_buf.valid = 0;
                    draw_all(); vio_flush(); continue;
                }
                if (k == 'p' && glyph > 0) {
                    glyph--; mx = glyph % CM_COLS; my = glyph / CM_COLS;
                    undo_buf.valid = 0;
                    draw_all(); vio_flush(); continue;
                }
                continue; /* unhandled -- no redraw */
            }
            draw_pixel_editor();
            draw_info();
            draw_titlebar();
            vio_flush();

        } else {
            /* PANEL_CHARMAP */
            switch (k) {
            case KEY_UP:
                if (my > 0) my--;
                break;
            case KEY_DOWN:
                if (my < CM_ROWS - 1) my++;
                break;
            case KEY_LEFT:
                if (mx > 0) mx--;
                else if (my > 0) { my--; mx = CM_COLS - 1; }
                break;
            case KEY_RIGHT:
                if (mx < CM_COLS - 1) mx++;
                else if (my < CM_ROWS - 1) { my++; mx = 0; }
                break;
            case KEY_HOME: mx = 0; break;
            case KEY_END:  mx = CM_COLS - 1; break;
            case KEY_CTRL_HOME: mx = 0; my = 0; break;
            case KEY_CTRL_END:  mx = CM_COLS-1; my = CM_ROWS-1; break;
            case ' ':
            case KEY_ENTER:
                glyph  = my * CM_COLS + mx;
                panel  = PANEL_EDITOR;
                px = py = 0;
                undo_buf.valid = 0;
                draw_all();
                vio_flush();
                continue;
            default:
                continue; /* no redraw */
            }
            glyph = my * CM_COLS + mx;
            draw_charmap();
            draw_pixel_editor();
            draw_info();
            draw_titlebar();
            vio_flush();
        }
    }

    vio_fini();
    vgaterm_close(vt);
    return 0;
}
