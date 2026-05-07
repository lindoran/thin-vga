/* SPDX-License-Identifier: MIT
 * vgaterm.c  --  VGA text-mode emulator for X11
 *
 * _POSIX_C_SOURCE 199309L is required for clock_gettime / struct timespec.
 */
#define _POSIX_C_SOURCE 199309L

/*
 * vgaterm.c  --  VGA text-mode emulator for X11
 *
 * Renders an 80x25 CP437 text buffer using the real 8x16 VGA bitmap font
 * into a 640x400 X11 window.  No font libraries required; everything is
 * done through Xlib with a single XImage that is blitted each frame.
 *
 * Build:
 *   cc -O2 -o vgaterm.o -c vgaterm.c $(pkg-config --cflags x11)
 */

#include "vgaterm.h"
#include "font_vga.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * CGA / VGA 16-colour palette  (RGB values matching real VGA hardware)
 * ------------------------------------------------------------------------- */
static const uint32_t CGA_PALETTE[16] = {
    0x000000, /* 0  Black        */
    0x0000AA, /* 1  Blue         */
    0x00AA00, /* 2  Green        */
    0x00AAAA, /* 3  Cyan         */
    0xAA0000, /* 4  Red          */
    0xAA00AA, /* 5  Magenta      */
    0xAA5500, /* 6  Brown        */
    0xAAAAAA, /* 7  Light Grey   */
    0x555555, /* 8  Dark Grey    */
    0x5555FF, /* 9  Light Blue   */
    0x55FF55, /* 10 Light Green  */
    0x55FFFF, /* 11 Light Cyan   */
    0xFF5555, /* 12 Light Red    */
    0xFF55FF, /* 13 Light Magenta*/
    0xFFFF55, /* 14 Yellow       */
    0xFFFFFF, /* 15 White        */
};

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */
struct VGATerm {
    Display     *dpy;
    Window       win;
    GC           gc;
    XImage      *img;          /* 640x400 backing image                    */
    Visual      *vis;
    int          depth;
    Atom         wm_del;       /* WM_DELETE_WINDOW atom                    */

    uint8_t      mem[VGA_MEMSIZE]; /* the text buffer, like 0xB8000        */

    /* pixel colours pre-mapped to display format */
    unsigned long px[16];

    /* cursor */
    int          cur_col;
    int          cur_row;
    int          cur_visible;  /* 1 if cursor drawn in current img         */
    struct timespec cur_time;  /* for blinking                             */

    int          alive;        /* 0 once window is closed                  */

    /* raw pixel buffer (32bpp BGRA or BGRX depending on display) */
    uint32_t    *pixels;       /* VGA_PX_W * VGA_PX_H uint32_t            */
};

/* -------------------------------------------------------------------------
 * Colour mapping
 * ------------------------------------------------------------------------- */
static unsigned long map_colour(Display *dpy, Visual *vis, uint32_t rgb)
{
    XColor xc;
    (void)vis;
    Colormap cm = DefaultColormap(dpy, DefaultScreen(dpy));
    xc.red   = ((rgb >> 16) & 0xFF) * 257;
    xc.green = ((rgb >>  8) & 0xFF) * 257;
    xc.blue  = ((rgb      ) & 0xFF) * 257;
    xc.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(dpy, cm, &xc);
    return xc.pixel;
}

/* -------------------------------------------------------------------------
 * Render one cell into the pixel buffer
 * ------------------------------------------------------------------------- */
static void render_cell(VGATerm *vt, int col, int row, int cursor_here)
{
    int offset = (row * VGA_COLS + col) * 2;
    uint8_t ch   = vt->mem[offset];
    uint8_t attr = vt->mem[offset + 1];

    int fg_idx = attr & 0x0F;
    int bg_idx = (attr >> 4) & 0x0F;

    /* If blinking bit (attr bit 7) is set in modes that support it, we just
     * treat it as high-intensity background.  Most text-mode apps use it
     * that way when BIOS blink is disabled. */

    unsigned long fg_px = vt->px[fg_idx];
    unsigned long bg_px = vt->px[bg_idx];

    const unsigned char *glyph = vga_font_8x16[ch];

    int base_x = col * 8;
    int base_y = row * 16;

    int r, b;
    for (r = 0; r < 16; r++) {
        unsigned char row_bits = glyph[r];
        uint32_t *scanline = vt->pixels + (base_y + r) * VGA_PX_W + base_x;
        for (b = 0; b < 8; b++) {
            int set = (row_bits >> (7 - b)) & 1;
            /* cursor inverts the cell */
            if (cursor_here) set = !set;
            scanline[b] = (uint32_t)(set ? fg_px : bg_px);
        }
    }
}

/* -------------------------------------------------------------------------
 * Full render of all 2000 cells
 * ------------------------------------------------------------------------- */
static void render_all(VGATerm *vt, int draw_cursor)
{
    int col, row;
    for (row = 0; row < VGA_ROWS; row++) {
        for (col = 0; col < VGA_COLS; col++) {
            int here = draw_cursor && (col == vt->cur_col) &&
                       (row == vt->cur_row) && (vt->cur_col >= 0);
            render_cell(vt, col, row, here);
        }
    }
}

/* -------------------------------------------------------------------------
 * Compute whether the cursor should be visible right now (500 ms blink)
 * ------------------------------------------------------------------------- */
static int cursor_blink_on(VGATerm *vt)
{
    struct timespec now;
    long ms;
    if (vt->cur_col < 0) return 0;
    clock_gettime(CLOCK_MONOTONIC, &now);
    ms = (long)((now.tv_sec  - vt->cur_time.tv_sec) * 1000 +
                (now.tv_nsec - vt->cur_time.tv_nsec) / 1000000);
    return (ms / 500) % 2 == 0;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

VGATerm *vgaterm_open(const char *title)
{
    VGATerm *vt;
    XSetWindowAttributes xswa;
    XSizeHints hints;
    int screen;
    int i;

    vt = (VGATerm *)calloc(1, sizeof(VGATerm));
    if (!vt) return NULL;

    vt->cur_col = 0;
    vt->cur_row = 0;
    vt->alive   = 1;
    clock_gettime(CLOCK_MONOTONIC, &vt->cur_time);

    /* Allocate pixel buffer (32bpp, width * height) */
    vt->pixels = (uint32_t *)malloc(VGA_PX_W * VGA_PX_H * sizeof(uint32_t));
    if (!vt->pixels) { free(vt); return NULL; }
    memset(vt->pixels, 0, VGA_PX_W * VGA_PX_H * sizeof(uint32_t));

    /* Open display */
    vt->dpy = XOpenDisplay(NULL);
    if (!vt->dpy) {
        fprintf(stderr, "vgaterm: XOpenDisplay failed\n");
        free(vt->pixels); free(vt); return NULL;
    }

    screen   = DefaultScreen(vt->dpy);
    vt->vis  = DefaultVisual(vt->dpy, screen);
    vt->depth= DefaultDepth(vt->dpy, screen);

    /* Pre-map the 16 palette colours */
    for (i = 0; i < 16; i++)
        vt->px[i] = map_colour(vt->dpy, vt->vis, CGA_PALETTE[i]);

    /* Create window: fixed size, no resize */
    xswa.background_pixel = vt->px[VGA_BLACK];
    xswa.border_pixel      = vt->px[VGA_BLACK];
    xswa.event_mask        = ExposureMask | StructureNotifyMask;

    vt->win = XCreateWindow(
        vt->dpy, RootWindow(vt->dpy, screen),
        0, 0, VGA_PX_W, VGA_PX_H, 0,
        vt->depth, InputOutput, vt->vis,
        CWBackPixel | CWBorderPixel | CWEventMask, &xswa
    );

    /* Lock the size */
    memset(&hints, 0, sizeof(hints));
    hints.flags      = PMinSize | PMaxSize | PBaseSize;
    hints.min_width  = hints.max_width  = hints.base_width  = VGA_PX_W;
    hints.min_height = hints.max_height = hints.base_height = VGA_PX_H;
    XSetWMNormalHints(vt->dpy, vt->win, &hints);

    /* Window title */
    XStoreName(vt->dpy, vt->win, title ? title : "VGA Terminal");

    /* WM_DELETE_WINDOW so we can intercept the close button */
    vt->wm_del = XInternAtom(vt->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(vt->dpy, vt->win, &vt->wm_del, 1);

    /* GC */
    vt->gc = XCreateGC(vt->dpy, vt->win, 0, NULL);

    /* XImage wrapping our pixel buffer */
    vt->img = XCreateImage(
        vt->dpy, vt->vis, vt->depth,
        ZPixmap, 0,
        (char *)vt->pixels,
        VGA_PX_W, VGA_PX_H,
        32,          /* bitmap_pad */
        VGA_PX_W * 4 /* bytes_per_line */
    );
    if (!vt->img) {
        fprintf(stderr, "vgaterm: XCreateImage failed\n");
        XDestroyWindow(vt->dpy, vt->win);
        XCloseDisplay(vt->dpy);
        free(vt->pixels); free(vt); return NULL;
    }

    XMapWindow(vt->dpy, vt->win);
    XFlush(vt->dpy);

    return vt;
}

void vgaterm_close(VGATerm *vt)
{
    if (!vt) return;
    /* Don't call XDestroyImage: it would try to free our pixels */
    vt->img->data = NULL;
    XDestroyImage(vt->img);
    XFreeGC(vt->dpy, vt->gc);
    XDestroyWindow(vt->dpy, vt->win);
    XCloseDisplay(vt->dpy);
    free(vt->pixels);
    free(vt);
}

uint8_t *vgaterm_mem(VGATerm *vt)
{
    return vt->mem;
}

void vgaterm_blit(VGATerm *vt)
{
    int draw_cur;
    if (!vt || !vt->alive) return;
    draw_cur = cursor_blink_on(vt);
    render_all(vt, draw_cur);
    XPutImage(vt->dpy, vt->win, vt->gc, vt->img,
              0, 0, 0, 0, VGA_PX_W, VGA_PX_H);
    XFlush(vt->dpy);
    vt->cur_visible = draw_cur;
}

int vgaterm_events(VGATerm *vt)
{
    XEvent ev;
    if (!vt) return 0;
    while (XPending(vt->dpy)) {
        XNextEvent(vt->dpy, &ev);
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0) {
                /* Re-blit after expose without re-rendering */
                XPutImage(vt->dpy, vt->win, vt->gc, vt->img,
                          0, 0, 0, 0, VGA_PX_W, VGA_PX_H);
                XFlush(vt->dpy);
            }
            break;
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == vt->wm_del) {
                vt->alive = 0;
                return 0;
            }
            break;
        default:
            break;
        }
    }
    return vt->alive;
}

void vgaterm_set_cursor(VGATerm *vt, int col, int row)
{
    if (!vt) return;
    vt->cur_col = col;
    vt->cur_row = row;
    clock_gettime(CLOCK_MONOTONIC, &vt->cur_time);
}

int vgaterm_fd(VGATerm *vt)
{
    return vt ? ConnectionNumber(vt->dpy) : -1;
}

/* -------------------------------------------------------------------------
 * Convenience helpers
 * ------------------------------------------------------------------------- */

void vgaterm_putc(VGATerm *vt, int col, int row, uint8_t ch, uint8_t attr)
{
    int off;
    if (!vt || col < 0 || col >= VGA_COLS || row < 0 || row >= VGA_ROWS) return;
    off = (row * VGA_COLS + col) * 2;
    vt->mem[off]     = ch;
    vt->mem[off + 1] = attr;
}

void vgaterm_puts(VGATerm *vt, int col, int row, const char *s, uint8_t attr)
{
    if (!vt || !s) return;
    while (*s && col < VGA_COLS) {
        vgaterm_putc(vt, col++, row, (uint8_t)*s++, attr);
    }
}

void vgaterm_cls(VGATerm *vt, uint8_t attr)
{
    int i;
    if (!vt) return;
    for (i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        vt->mem[i * 2]     = 0x20; /* space */
        vt->mem[i * 2 + 1] = attr;
    }
}

void vgaterm_scroll(VGATerm *vt, int n, uint8_t attr)
{
    int i;
    int row_bytes;
    if (!vt || n <= 0) return;
    if (n >= VGA_ROWS) { vgaterm_cls(vt, attr); return; }
    row_bytes = VGA_COLS * 2;
    memmove(vt->mem, vt->mem + n * row_bytes,
            (VGA_ROWS - n) * row_bytes);
    /* blank the scrolled-in lines */
    for (i = (VGA_ROWS - n) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++) {
        vt->mem[i * 2]     = 0x20;
        vt->mem[i * 2 + 1] = attr;
    }
}

/* ---------- X11 accessors ----------------------------------------- */
Display *vgaterm_display(VGATerm *vt) { return vt->dpy; }
Window   vgaterm_window (VGATerm *vt) { return vt->win; }
