/* SPDX-License-Identifier: MIT
 * editor.c  --  minimal full-screen editor demo using vio
 *
 * Demonstrates the complete stack: vgaterm → vio → editor
 *
 * Keys:
 *   Arrows         move cursor
 *   Printable      insert character at cursor (overwrite mode)
 *   Backspace      erase left
 *   Ctrl+Q         quit
 *   Ctrl+L         redraw screen
 *   F1             toggle help bar
 *
 * This is intentionally bare: no file I/O, no undo, no scroll.
 * It just proves the I/O layer is right for building on.
 *
 * Build:  make editor
 * Run:    ./editor
 */
#define _POSIX_C_SOURCE 199309L

#include "vgaterm.h"
#include "vio.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * A trivial flat text buffer: 80 chars per line, 23 editable rows
 * (rows 0..22), with a title bar at row 0 and status bar at row 24.
 * ----------------------------------------------------------------------- */

#define EDIT_ROWS  23
#define EDIT_COLS  VGA_COLS   /* 80 */
#define EDIT_TOP   1          /* first editable screen row */

static char buf[EDIT_ROWS][EDIT_COLS + 1];  /* +1 for NUL */

/* Colour scheme */
#define A_NORMAL    VGA_ATTR(VGA_BLACK,  VGA_LGRAY)
#define A_TITLEBAR  VGA_ATTR(VGA_BLUE,   VGA_WHITE)
#define A_STATUSBAR VGA_ATTR(VGA_CYAN,   VGA_BLACK)
#define A_HILITBAR  VGA_ATTR(VGA_BLACK,  VGA_YELLOW)
#define A_CURSOR_LN VGA_ATTR(VGA_DGRAY,  VGA_WHITE)

/* Editor state */
static int cur_col = 0;   /* 0..EDIT_COLS-1 */
static int cur_row = 0;   /* 0..EDIT_ROWS-1 */
static int show_help = 1;

/* -----------------------------------------------------------------------
 * Rendering
 * ----------------------------------------------------------------------- */

static void draw_titlebar(void)
{
    int i;
    vio_gotoxy(0, 0);
    vio_setattr(A_TITLEBAR);
    for (i = 0; i < VGA_COLS; i++)
        vio_putch(' ');
    vio_gotoxy(2, 0);
    vio_puts("vio editor demo");
    {
        char pos[32];
        snprintf(pos, sizeof(pos), "Ln %2d  Col %2d", cur_row + 1, cur_col + 1);
        vio_gotoxy(VGA_COLS - (int)strlen(pos) - 1, 0);
        vio_puts(pos);
    }
}

static void draw_statusbar(void)
{
    int i;
    vio_gotoxy(0, 24);
    vio_setattr(A_STATUSBAR);
    for (i = 0; i < VGA_COLS; i++)
        vio_putch(' ');
    vio_gotoxy(1, 24);
    if (show_help)
        vio_puts("^Q Quit  ^L Redraw  F1 Help  Arrows Move  Printable Insert");
    else
        vio_puts("F1 Show keys");
}

static void draw_line(int row)
{
    uint8_t attr = (row == cur_row) ? A_CURSOR_LN : A_NORMAL;
    vio_gotoxy(0, EDIT_TOP + row);
    vio_setattr(attr);
    vio_puts_n(buf[row], EDIT_COLS);
}

static void draw_all(void)
{
    int r;
    draw_titlebar();
    for (r = 0; r < EDIT_ROWS; r++)
        draw_line(r);
    draw_statusbar();
}

static void place_cursor(void)
{
    vio_gotoxy(cur_col, EDIT_TOP + cur_row);
    vio_show_cursor();
}

/* -----------------------------------------------------------------------
 * Buffer helpers
 * ----------------------------------------------------------------------- */

static int line_len(int row)
{
    /* logical length (trailing spaces don't count) */
    int n = EDIT_COLS;
    while (n > 0 && buf[row][n - 1] == ' ')
        n--;
    return n;
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void)
{
    int r, k;
    int prev_row;

    /* initialise buffer to spaces */
    for (r = 0; r < EDIT_ROWS; r++) {
        memset(buf[r], ' ', EDIT_COLS);
        buf[r][EDIT_COLS] = '\0';
    }

    /* seed a little welcome text */
    {
        const char *msg = "Welcome to the vio editor demo.  Start typing.";
        size_t len = strlen(msg);
        size_t i;
        for (i = 0; i < len && i < EDIT_COLS; i++)
            buf[2][i] = msg[i];
    }

    /* --- bring up the window --- */
    VGATerm *vt = vgaterm_open("vio editor demo");
    if (!vt) return 1;

    vio_init(vt);
    vio_setattr(A_NORMAL);
    vio_clrscr();

    draw_all();
    place_cursor();
    vio_flush();

    /* --- event loop --- */
    for (;;) {
        prev_row = cur_row;
        k = vio_getch();

        if (k == KEY_CLOSED)        break;
        if (k == KEY_CTRL('q'))     break;

        switch (k) {

        /* --- navigation --- */
        case KEY_UP:
            if (cur_row > 0) cur_row--;
            if (cur_col > line_len(cur_row)) cur_col = line_len(cur_row);
            break;
        case KEY_DOWN:
            if (cur_row < EDIT_ROWS - 1) cur_row++;
            if (cur_col > line_len(cur_row)) cur_col = line_len(cur_row);
            break;
        case KEY_LEFT:
            if (cur_col > 0) cur_col--;
            else if (cur_row > 0) { cur_row--; cur_col = line_len(cur_row); }
            break;
        case KEY_RIGHT:
            if (cur_col < EDIT_COLS - 1) cur_col++;
            break;
        case KEY_HOME:
            cur_col = 0;
            break;
        case KEY_END:
            cur_col = line_len(cur_row);
            break;
        case KEY_CTRL_HOME:
            cur_col = 0; cur_row = 0;
            break;
        case KEY_CTRL_END:
            cur_row = EDIT_ROWS - 1;
            cur_col = line_len(cur_row);
            break;
        case KEY_PGUP:
            cur_row = 0;
            break;
        case KEY_PGDN:
            cur_row = EDIT_ROWS - 1;
            break;

        /* --- editing --- */
        case KEY_BS:
            if (cur_col > 0) {
                int i;
                cur_col--;
                /* shift chars left */
                for (i = cur_col; i < EDIT_COLS - 1; i++)
                    buf[cur_row][i] = buf[cur_row][i + 1];
                buf[cur_row][EDIT_COLS - 1] = ' ';
            }
            break;
        case KEY_DEL:
            if (cur_col < EDIT_COLS - 1) {
                int i;
                for (i = cur_col; i < EDIT_COLS - 1; i++)
                    buf[cur_row][i] = buf[cur_row][i + 1];
                buf[cur_row][EDIT_COLS - 1] = ' ';
            }
            break;

        /* --- commands --- */
        case KEY_CTRL('l'):
            /* force full redraw */
            draw_all();
            break;

        case KEY_F1:
            show_help = !show_help;
            draw_statusbar();
            break;

        default:
            /* printable ASCII: overwrite at cursor */
            if (k >= 0x20 && k < 0x7F && cur_col < EDIT_COLS) {
                buf[cur_row][cur_col] = (char)k;
                if (cur_col < EDIT_COLS - 1) cur_col++;
            }
            break;
        }

        /* redraw changed lines */
        if (prev_row != cur_row) {
            draw_line(prev_row);
            draw_line(cur_row);
        } else {
            draw_line(cur_row);
        }
        draw_titlebar();
        place_cursor();
        vio_flush();
    }

    vio_fini();
    vgaterm_close(vt);
    return 0;
}
