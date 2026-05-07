/* SPDX-License-Identifier: MIT
 * vgaterm.h  --  VGA text-mode emulator for X11
 *
 * Emulates VGA mode 3: 80x25 text, CP437 charset, 16-colour CGA palette.
 *
 * Memory layout (identical to real VGA at 0xB8000):
 *   Each cell is 2 bytes:  [character byte][attribute byte]
 *   Attribute:  bits 7-4 = background colour, bits 3-0 = foreground colour
 *   Colour index 0-15: standard CGA/VGA palette
 *
 *   cell(col, row) is at offset  (row * VGA_COLS + col) * 2
 *
 * Typical usage:
 *
 *   VGATerm *vt = vgaterm_open("My App");
 *   uint8_t *mem = vgaterm_mem(vt);          // pointer to the 4000-byte buffer
 *
 *   // write 'A' in bright white on blue at (0,0)
 *   mem[0] = 'A';
 *   mem[1] = (1 << 4) | 15;                 // bg=blue(1), fg=white(15)
 *
 *   vgaterm_blit(vt);                        // flush to window
 *
 *   while (vgaterm_events(vt)) {             // returns 0 when window is closed
 *       // ... update mem ...
 *       vgaterm_blit(vt);
 *   }
 *
 *   vgaterm_close(vt);
 */

#ifndef VGATERM_H
#define VGATERM_H

#include <stdint.h>

#define VGA_COLS    80
#define VGA_ROWS    25
#define VGA_MEMSIZE (VGA_COLS * VGA_ROWS * 2)   /* 4000 bytes */

/* Pixel dimensions of the window */
#define VGA_PX_W    (VGA_COLS * 8)   /* 640 */
#define VGA_PX_H    (VGA_ROWS * 16)  /* 400 */

/* Standard CGA attribute byte colour indices */
#define VGA_BLACK         0
#define VGA_BLUE          1
#define VGA_GREEN         2
#define VGA_CYAN          3
#define VGA_RED           4
#define VGA_MAGENTA       5
#define VGA_BROWN         6
#define VGA_LGRAY         7
#define VGA_DGRAY         8
#define VGA_LBLUE         9
#define VGA_LGREEN       10
#define VGA_LCYAN        11
#define VGA_LRED         12
#define VGA_LMAGENTA     13
#define VGA_YELLOW       14
#define VGA_WHITE        15

/* Build an attribute byte: VGA_ATTR(bg, fg) */
#define VGA_ATTR(bg, fg)  (uint8_t)(((bg) & 0x0F) << 4 | ((fg) & 0x0F))

/* Default attribute: light grey on black (what you see on boot) */
#define VGA_ATTR_DEFAULT  VGA_ATTR(VGA_BLACK, VGA_LGRAY)

/* Opaque handle */
typedef struct VGATerm VGATerm;

/*
 * vgaterm_open  --  create a window and return a handle.
 * title         --  window title string (UTF-8).
 * Returns NULL on failure.
 */
VGATerm *vgaterm_open(const char *title);

/*
 * vgaterm_close  --  destroy the window and free all resources.
 */
void vgaterm_close(VGATerm *vt);

/*
 * vgaterm_mem  --  return a pointer to the 4000-byte VGA text buffer.
 * Write character/attribute pairs here, then call vgaterm_blit().
 * The buffer is zeroed on creation.
 */
uint8_t *vgaterm_mem(VGATerm *vt);

/*
 * vgaterm_blit  --  render the current text buffer to the X11 window.
 * Call this whenever the buffer changes.
 */
void vgaterm_blit(VGATerm *vt);

/*
 * vgaterm_events  --  process pending X11 events (expose, resize, WM delete).
 * Returns 1 if the window is still open, 0 if it has been closed.
 * Call this in your main loop; it is non-blocking.
 */
int vgaterm_events(VGATerm *vt);

/*
 * vgaterm_set_cursor  --  position the blinking block cursor.
 * col: 0..79,  row: 0..24.
 * Pass col=-1 to hide the cursor.
 */
void vgaterm_set_cursor(VGATerm *vt, int col, int row);

/* ---------- convenience helpers ---------- */

/*
 * vgaterm_putc  --  write one character+attribute at (col, row).
 */
void vgaterm_putc(VGATerm *vt, int col, int row, uint8_t ch, uint8_t attr);

/*
 * vgaterm_puts  --  write a NUL-terminated ASCII string starting at (col, row).
 * Does NOT wrap or scroll.
 */
void vgaterm_puts(VGATerm *vt, int col, int row, const char *s, uint8_t attr);

/*
 * vgaterm_cls  --  clear the screen with the given attribute.
 */
void vgaterm_cls(VGATerm *vt, uint8_t attr);

/*
 * vgaterm_scroll  --  scroll the screen up by n lines, filling the bottom
 * with blank cells using the given attribute.
 */
void vgaterm_scroll(VGATerm *vt, int n, uint8_t attr);

/*
 * vgaterm_fd  --  return the X11 connection file descriptor so you can
 * use select()/poll() in your main loop instead of busy-waiting.
 */
int vgaterm_fd(VGATerm *vt);

#endif /* VGATERM_H */

/* ---------- X11 accessors (for composition with higher layers) ---------- */

/*
 * These expose the raw Xlib handles so a layer above (e.g. vio) can
 * call XSelectInput / XNextEvent without reimplementing the connection.
 * They are read-only; do not close or destroy the returned objects.
 */
#include <X11/Xlib.h>
Display *vgaterm_display(VGATerm *vt);
Window   vgaterm_window (VGATerm *vt);
