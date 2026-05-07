/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 199309L
/*
 * demo.c  --  VGA terminal emulator demo
 */
#define _POSIX_C_SOURCE 199309L

/*
 * demo.c  --  VGA terminal emulator demo (continued)
 *
 * Shows the full CP437 character set, the 16-colour palette, and a simple
 * animated sine-wave banner, all rendered via the raw 4000-byte text buffer.
 *
 * Build:  make demo
 * Run:    ./demo
 */

#include "vgaterm.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

/* ---- helpers ---- */
static void ms_sleep(int ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static long ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ---- draw a box with double-line borders ---- */
/*  CP437: ╔=0xC9 ╗=0xBB ╚=0xC8 ╝=0xBC ═=0xCD ║=0xBA  */
static void draw_box(VGATerm *vt, int x, int y, int w, int h, uint8_t attr)
{
    int i;
    vgaterm_putc(vt, x,       y,       0xC9, attr);
    vgaterm_putc(vt, x+w-1,   y,       0xBB, attr);
    vgaterm_putc(vt, x,       y+h-1,   0xC8, attr);
    vgaterm_putc(vt, x+w-1,   y+h-1,   0xBC, attr);
    for (i = 1; i < w-1; i++) {
        vgaterm_putc(vt, x+i, y,       0xCD, attr);
        vgaterm_putc(vt, x+i, y+h-1,   0xCD, attr);
    }
    for (i = 1; i < h-1; i++) {
        vgaterm_putc(vt, x,     y+i,   0xBA, attr);
        vgaterm_putc(vt, x+w-1, y+i,   0xBA, attr);
    }
}

/* ---- centre a string inside a box interior ---- */
static void box_title(VGATerm *vt, int bx, int by, int bw,
                      const char *s, uint8_t attr)
{
    int len = (int)strlen(s);
    int col = bx + 1 + (bw - 2 - len) / 2;
    vgaterm_puts(vt, col, by, s, attr);
}

/* ---- palette swatch bar ---- */
static void draw_palette(VGATerm *vt, int row)
{
    int i;
    for (i = 0; i < 16; i++) {
        int col = 30 + i * 2;
        vgaterm_putc(vt, col,   row, 0xDB, VGA_ATTR(VGA_BLACK, i));  /* fg colour */
        vgaterm_putc(vt, col+1, row, 0xDB, VGA_ATTR(i, VGA_BLACK));  /* bg colour */
    }
}

/* ---- dump all 256 CP437 glyphs ---- */
static void draw_charset(VGATerm *vt, int bx, int by)
{
    int c;
    for (c = 0; c < 256; c++) {
        int col = bx + (c % 32);
        int row = by + (c / 32);
        uint8_t attr = VGA_ATTR(VGA_BLACK, VGA_LGRAY);
        /* colour the control-char row differently */
        if (c < 32)  attr = VGA_ATTR(VGA_BLACK, VGA_LCYAN);
        if (c > 127 && c < 160) attr = VGA_ATTR(VGA_BLACK, VGA_YELLOW);
        vgaterm_putc(vt, col, row, (uint8_t)c, attr);
    }
}

/* ---- animated sine-wave scroller ---- */
static const char BANNER[] =
    "  ** vgaterm: VGA text-mode emulator for X11 **   "
    "  80x25  |  CP437  |  8x16 bitmap font  |  16 colours  "
    "  Direct 4000-byte text buffer  -- no intermediate layers  ";

static void draw_scroller(VGATerm *vt, int row, double t)
{
    double phase;
    int banner_len = (int)strlen(BANNER);
    int col;
    for (col = 0; col < VGA_COLS; col++) {
        /* vertical sine offset */
        phase = t * 2.0 + col * 0.25;
        int dy = (int)(sin(phase) * 3.0 + 0.5);
        int r  = row + dy;
        if (r < 0 || r >= VGA_ROWS) {
            vgaterm_putc(vt, col, row, ' ', VGA_ATTR_DEFAULT);
            continue;
        }
        int banner_idx = ((int)(t * 12) + col) % banner_len;
        if (banner_idx < 0) banner_idx += banner_len;
        uint8_t ch   = (uint8_t)BANNER[banner_idx];
        /* cycle through bright colours based on column */
        uint8_t fg   = (uint8_t)(8 + ((col + (int)(t * 4)) % 8));
        uint8_t attr = VGA_ATTR(VGA_BLACK, fg);
        vgaterm_putc(vt, col, r, ch, attr);
    }
}

/* ---- shade block art gradient ---- */
/*  CP437 shades: 0xB0=░  0xB1=▒  0xB2=▓  0xDB=█  */
static void draw_shade_demo(VGATerm *vt, int bx, int by, int bw)
{
    static const uint8_t shades[] = { 0xB0, 0xB1, 0xB2, 0xDB };
    int i, s;
    for (i = 0; i < 16; i++) {
        for (s = 0; s < 4; s++) {
            int col = bx + i * 4 + s;
            if (col >= bx + bw) break;
            vgaterm_putc(vt, col, by, shades[s], VGA_ATTR(VGA_BLACK, i));
        }
    }
}

int main(void)
{
    VGATerm *vt = vgaterm_open("VGA Terminal Demo");
    long start;
    int frame;
    double t;

    if (!vt) {
        fprintf(stderr, "Failed to open VGA terminal\n");
        return 1;
    }


    /* ---- static layout ---- */
    vgaterm_cls(vt, VGA_ATTR(VGA_BLACK, VGA_LGRAY));

    /* title bar */
    {
        int i;
        for (i = 0; i < VGA_COLS; i++)
            vgaterm_putc(vt, i, 0, ' ', VGA_ATTR(VGA_BLUE, VGA_WHITE));
        vgaterm_puts(vt, 1, 0, "VGA Text Mode Emulator", VGA_ATTR(VGA_BLUE, VGA_YELLOW));
        vgaterm_puts(vt, 57, 0, "80x25  CP437  8x16", VGA_ATTR(VGA_BLUE, VGA_WHITE));
    }

    /* CP437 charset box */
    draw_box(vt, 0, 2, 34, 12, VGA_ATTR(VGA_BLACK, VGA_CYAN));
    box_title(vt, 0, 2, 34, "\xCD CP437 \xCD", VGA_ATTR(VGA_BLACK, VGA_YELLOW));
    draw_charset(vt, 1, 3);

    /* Colour palette box */
    draw_box(vt, 0, 14, 66, 5, VGA_ATTR(VGA_BLACK, VGA_GREEN));
    box_title(vt, 0, 14, 66, "\xCD Colours & Shade \xCD", VGA_ATTR(VGA_BLACK, VGA_YELLOW));
    draw_palette(vt, 16);
    draw_shade_demo(vt, 1, 17, 64);

    /* attrs demo: every fg on a dark bg */
    {
        int i;
        for (i = 0; i < 16; i++) {
            char label[3];
            label[0] = "0123456789ABCDEF"[i];
            label[1] = ' ';
            label[2] = '\0';
            vgaterm_puts(vt, 35 + i*2, 4, label,
                         VGA_ATTR(VGA_DGRAY, (uint8_t)i));
        }
    }

    /* box drawing demo */
    draw_box(vt, 35, 2, 22, 12, VGA_ATTR(VGA_BLACK, VGA_MAGENTA));
    box_title(vt, 35, 2, 22, " Box Art ", VGA_ATTR(VGA_BLACK, VGA_YELLOW));
    {
        /* single-line inner box: ┌=0xDA ┐=0xBF └=0xC0 ┘=0xD9 ─=0xC4 │=0xB3 */
        vgaterm_putc(vt, 37,  6, 0xDA, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
        vgaterm_putc(vt, 44,  6, 0xBF, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
        vgaterm_putc(vt, 37, 11, 0xC0, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
        vgaterm_putc(vt, 44, 11, 0xD9, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
        int i;
        for (i = 1; i < 7; i++) {
            vgaterm_putc(vt, 37+i,  6, 0xC4, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
            vgaterm_putc(vt, 37+i, 11, 0xC4, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
        }
        for (i = 1; i < 5; i++) {
            vgaterm_putc(vt, 37,  6+i, 0xB3, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
            vgaterm_putc(vt, 44,  6+i, 0xB3, VGA_ATTR(VGA_BLACK, VGA_LCYAN));
        }
        vgaterm_puts(vt, 38,  7, "Single", VGA_ATTR(VGA_BLACK, VGA_LGREEN));
        vgaterm_puts(vt, 38,  8, " line ", VGA_ATTR(VGA_BLACK, VGA_LGREEN));
        vgaterm_puts(vt, 38,  9, " box  ", VGA_ATTR(VGA_BLACK, VGA_LGREEN));
    }

    /* info box */
    draw_box(vt, 57, 2, 23, 12, VGA_ATTR(VGA_BLACK, VGA_LGRAY));
    box_title(vt, 57, 2, 23, " Info ", VGA_ATTR(VGA_BLACK, VGA_YELLOW));
    vgaterm_puts(vt, 59,  4, "Window: 640x400", VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59,  5, "Cols:   80",      VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59,  6, "Rows:   25",      VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59,  7, "Font:   8x16",    VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59,  8, "Chars:  256",     VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59,  9, "Colors: 16",      VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59, 10, "Buf:    4000 B",  VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59, 11, "Encode: CP437",   VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vgaterm_puts(vt, 59, 12, "Blit:   XImage",  VGA_ATTR(VGA_BLACK, VGA_WHITE));

    /* status bar at bottom */
    {
        int i;
        for (i = 0; i < VGA_COLS; i++)
            vgaterm_putc(vt, i, 24, ' ', VGA_ATTR(VGA_CYAN, VGA_BLACK));
        vgaterm_puts(vt, 1, 24,
            "Close window or press Ctrl-C to quit",
            VGA_ATTR(VGA_CYAN, VGA_BLACK));
    }

    vgaterm_set_cursor(vt, 0, 24);

    /* ---- animation loop ---- */
    start = ms_now();
    frame = 0;

    while (vgaterm_events(vt)) {
        long now = ms_now();
        t = (now - start) / 1000.0;

        /* clear scroller rows first */
        {
            int r;
            for (r = 19; r <= 23; r++) {
                int c;
                for (c = 0; c < VGA_COLS; c++)
                    vgaterm_putc(vt, c, r, ' ', VGA_ATTR_DEFAULT);
            }
        }

        /* animated scroller centred around row 21 */
        draw_scroller(vt, 21, t);

        /* frame counter in status bar */
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "t=%.1fs  frame=%d", t, frame);
            vgaterm_puts(vt, 43, 24, buf, VGA_ATTR(VGA_CYAN, VGA_BLACK));
        }

        vgaterm_blit(vt);
        frame++;
        ms_sleep(33);  /* ~30 fps */
    }

    vgaterm_close(vt);
    return 0;
}
