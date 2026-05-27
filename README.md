# Thin-VGA

A minimal VGA text-mode emulator for X11 — retro computing for modern systems.

Gives you an **80×25 window** with the authentic **8×16 VGA bitmap font**, **16-color CGA palette**, and a **direct 4000-byte memory buffer** — just like VGA text mode 3 at physical address `0xB8000`. No ncurses, no PDCurses, no font libraries. Just you and raw text buffer access.

Perfect for **porting retro applications**, **building nostalgic UIs**, or **understanding classic text-mode programming**.

## Philosophy

PDCursesmod is excellent and feature complete.  For porting a curses application to SDL it's perfect, however for porting a terminal application that's written in C, but draws directly to the display file for VGA text mode it's SLOW.  For that you need something thin, if you are already bringing your own IO, logic loops, cursor management or you just want something that isn't a thousand tiny interpreters running to generate a buffer for your text editor or bespoke EDA from 1987, this might be far better.

Thin VGA is tiny—300 lines of code, plus a font. It basically just gives you a bag of memory and a state machine that pumps out pixels. This is for if you want to bring over a text mode interface that already resides on top of a framebuffer, but don't want to try to make a thin curses front end.

Thin VGA won't let you make poor coding choices like curses will. You simply can't use stdio or conio on top of Thin VGA, at least without a shim. This makes every decision intentional and deterministic. You have to actualize the screen as a block of memory, and build your logic around that. This keeps the interface clean and lets you decide what resources you want to use. X Windows is designed to be purpose-built to draw boxes around frame buffers. This doesn't try to reinvent the wheel—it merely makes a simple wheel that's very useful for a specific subset of tasks (and so it's an excellent building block for other things!)


## Use Cases

- **Retro application porting** — Bring classic DOS/UNIX terminal applications to X11 with authentic visuals
- **Nostalgic UI design** — Add retro flair to modern applications with genuine 8×16 bitmap aesthetics
- **Educational** — Learn VGA text-mode programming without dealing with real hardware or complex emulators
- **Embedded terminal** — Perfect for systems tools, configuration UIs, or real-time monitoring dashboards
- **Terminal emulation** — Build a lightweight, authentic text terminal without heavy dependencies

## Features

- **Authentic VGA emulation** — Real 80×25 text mode with genuine CP437 character set
- **Direct memory access** — Write to a 4000-byte buffer exactly like real VGA (0xB8000)
- **16-color palette** — Full CGA color support with true bitmap rendering
- **Zero dependencies** — Only requires X11
- **Box-drawing support** — Includes all CP437 box-drawing and symbol characters (☺ ☻ ♥ ♦ …)
- **Minimal code** — ~300 lines, perfect for learning or embedding
- **select()/poll() ready** — Integrates cleanly with event loops

## Quick Start

```c
#include "vgaterm.h"

int main(void) {
    VGATerm *vt = vgaterm_open("Hello VGA");
    uint8_t *mem = vgaterm_mem(vt);

    mem[0] = 'H';  mem[1] = 0x0F;   /* 'H' in white on black */
    vgaterm_blit(vt);

    while (vgaterm_events(vt)) { }  /* run until closed */
    vgaterm_close(vt);
}
```

Build with: `make` and `link with -lX11 -lm`

## Table of Contents

- [Philosophy](#philosophy)
- [Use Cases](#use-cases)
- [Features](#features)
- [Quick Start](#quick-start)
- [Installation](#installation)
- [Building & Demo](#building--demo)
- [Memory Layout](#memory-layout)
- [API Reference](#api-reference)
- [Integration](#integration)
- [Font Regeneration](#font-regeneration)
- [VIO Layer (Optional)](#vio-layer-optional)
- [Advanced Usage](#advanced-usage)
- [Technical Details](#technical-details)


## Memory Layout

```
uint8_t mem[80 * 25 * 2];   /* 4000 bytes */

/* cell at column c, row r: */
mem[(r * 80 + c) * 2 + 0]  = character byte  (CP437)
mem[(r * 80 + c) * 2 + 1]  = attribute byte  (bg<<4 | fg)
```

Attribute byte colour indices (0–15) are the standard CGA palette:

| Index | Colour       | Index | Colour        |
|-------|--------------|-------|---------------|
| 0     | Black        | 8     | Dark Grey     |
| 1     | Blue         | 9     | Light Blue    |
| 2     | Green        | 10    | Light Green   |
| 3     | Cyan         | 11    | Light Cyan    |
| 4     | Red          | 12    | Light Red     |
| 5     | Magenta      | 13    | Light Magenta |
| 6     | Brown        | 14    | Yellow        |
| 7     | Light Grey   | 15    | White         |

### Color Constants and Macros

For convenience, use these constants instead of magic numbers:

```c
#define VGA_BLACK      0
#define VGA_BLUE       1
#define VGA_GREEN      2
#define VGA_CYAN       3
#define VGA_RED        4
#define VGA_MAGENTA    5
#define VGA_BROWN      6
#define VGA_LGRAY      7
#define VGA_DGRAY      8
#define VGA_LBLUE      9
#define VGA_LGREEN     10
#define VGA_LCYAN      11
#define VGA_LRED       12
#define VGA_LMAGENTA   13
#define VGA_YELLOW     14
#define VGA_WHITE      15

/* Build attribute byte: VGA_ATTR(background, foreground) */
#define VGA_ATTR(bg, fg)  (uint8_t)(((bg) & 0x0F) << 4 | ((fg) & 0x0F))

/* Default: light grey on black */
#define VGA_ATTR_DEFAULT  VGA_ATTR(VGA_BLACK, VGA_LGRAY)
```

**Example:**

```c
uint8_t attr = VGA_ATTR(VGA_BLUE, VGA_WHITE);  /* white text on blue background */
mem[0] = 'A';
mem[1] = attr;
```

## API Reference

```c
VGATerm *vgaterm_open(const char *title);   /* create window           */
void     vgaterm_close(VGATerm *vt);        /* destroy window          */

uint8_t *vgaterm_mem(VGATerm *vt);          /* pointer to 4000-B buf   */
void     vgaterm_blit(VGATerm *vt);         /* flush buffer to screen  */
int      vgaterm_events(VGATerm *vt);       /* pump events; 0=closed   */

void     vgaterm_set_cursor(VGATerm *vt, int col, int row);
void     vgaterm_putc(VGATerm *vt, int col, int row, uint8_t ch, uint8_t attr);
void     vgaterm_puts(VGATerm *vt, int col, int row, const char *s, uint8_t attr);
void     vgaterm_cls(VGATerm *vt, uint8_t attr);
void     vgaterm_scroll(VGATerm *vt, int n, uint8_t attr);
int      vgaterm_fd(VGATerm *vt);           /* X11 fd for select/poll  */
```

## Installation

### Prerequisites

```bash
# Debian / Ubuntu
sudo apt install libx11-dev

# Fedora / RHEL
sudo dnf install libX11-devel

# Arch
sudo pacman -S libx11
```

### Building & Demo

```bash
make          # generates font_vga.h, builds libvgaterm.a and vgaterm_demo
make demo     # also runs the demo
```

## Font Regeneration

`font_vga.h` is pre-generated and committed to the repo — you do not need to
regenerate it under normal circumstances.

If you do need to regenerate it (e.g. after modifying `mkfont.py`):

```bash
sudo apt install python3-fonttools
python3 mkfont.py Bm437_IBM_VGA_8x16.otb > font_vga.h
```

The script requires `Bm437_IBM_VGA_8x16.otb` to be present alongside it and
will exit with a clear error if it is missing. It does not fall back to system
fonts — that was the original source of incorrect box-drawing characters in
earlier versions of this project.

> **If box-drawing characters look wrong in your application, `font_vga.h` is
> the first place to check.** Regenerate it from the OTB using the command
> above and verify the output.

## Integration

Copy `vgaterm.h`, `vgaterm.c`, and `font_vga.h` into your source tree.

```c
#include "vgaterm.h"

int main(void) {
    VGATerm *vt = vgaterm_open("My App");
    uint8_t *mem = vgaterm_mem(vt);

    /* Write directly to the buffer like real VGA */
    mem[0] = 'H';  mem[1] = 0x0F;   /* white on black */
    mem[2] = 'i';  mem[3] = 0x0E;   /* yellow on black */

    vgaterm_blit(vt);

    while (vgaterm_events(vt)) {
        /* your update loop here */
        vgaterm_blit(vt);
    }

    vgaterm_close(vt);
    return 0;
}
```

Link with `-lX11 -lm`.

## VIO Layer (Optional)

For editor-like applications with keyboard input, cursor tracking, and text output, `vio.h` provides a convenience layer on top of `vgaterm`. It handles input event processing, maintains cursor position/attribute state, and offers text output helpers.

### Initialization

```c
VGATerm *vt = vgaterm_open("myeditor");
vio_init(vt);           /* attach vio to the VGATerm */
vio_clrscr();           /* clear screen */
vio_flush();            /* update display */

/* ... main loop ... */

vio_fini();             /* cleanup vio */
vgaterm_close(vt);      /* close window */
```

### Output Functions

```c
/* Position and attributes */
void vio_gotoxy(int col, int row);      /* set cursor position */
int  vio_col(void);                     /* get current column */
int  vio_row(void);                     /* get current row */
void vio_setattr(uint8_t attr);         /* set output attribute (color) */
uint8_t vio_attr(void);                 /* get current attribute */

/* Character/string output (uses current position + attribute) */
void vio_putch(uint8_t ch);             /* write char at cursor, advance */
void vio_putch_at(int col, int row, uint8_t ch, uint8_t attr);
void vio_puts(const char *s);           /* write string at cursor */
void vio_puts_n(const char *s, int n);  /* write n chars (space-padded) */

/* Screen operations */
void vio_clrscr(void);                  /* clear screen, reset to (0,0) */
void vio_clreol(void);                  /* clear to end of line */
void vio_clrline(int row, uint8_t attr); /* clear entire row */
void vio_fill(int col, int row, int w, int h, uint8_t ch, uint8_t attr);

/* Cursor visibility */
void vio_show_cursor(void);             /* show blinking cursor at tracked position */
void vio_hide_cursor(void);             /* hide cursor */
void vio_flush(void);                   /* blit buffer to screen (vgaterm_blit) */

/* Numeric formatting */
void vio_int(int n, int width);         /* print signed int, right-justified */
void vio_uint(unsigned int n, int width); /* print unsigned int */
void vio_hex(unsigned int n, int width);  /* print hex, zero-padded */

/* Window management */
void vio_set_title(const char *title);  /* update window title */
```

### Input: Key Codes and Functions

```c
/* Blocking input */
int vio_getch(void);                    /* wait for key, return key code */

/* Non-blocking input */
int vio_kbhit(void);                    /* return pending key or KEY_NONE */
```

**Key code constants:**

```c
/* ASCII printable and control codes (0x00-0x7F) returned as-is */
#define KEY_CTRL(c)    ((c) & 0x1F)     /* Ctrl+key: KEY_CTRL('q') == 0x11 */
#define KEY_BS         0x08             /* Backspace */
#define KEY_TAB        0x09             /* Tab */
#define KEY_ENTER      0x0D             /* Enter */
#define KEY_ESC        0x1B             /* Escape */

/* Special keys (>= 0x0100) */
#define KEY_DEL        0x0100           /* Delete */
#define KEY_UP         0x0101           /* Arrow Up */
#define KEY_DOWN       0x0102           /* Arrow Down */
#define KEY_LEFT       0x0103           /* Arrow Left */
#define KEY_RIGHT      0x0104           /* Arrow Right */
#define KEY_HOME       0x0105
#define KEY_END        0x0106
#define KEY_PGUP       0x0107
#define KEY_PGDN       0x0108
#define KEY_INS        0x0109

#define KEY_F1..KEY_F12   0x0110..0x011B  /* Function keys F1–F12 */
#define KEY_SHIFT_TAB   0x011C           /* Shift+Tab (back-tab) */

/* Shift + arrows (for selection) */
#define KEY_SHIFT_UP   0x0120
#define KEY_SHIFT_DOWN 0x0121
#define KEY_SHIFT_LEFT 0x0122
#define KEY_SHIFT_RIGHT 0x0123

/* Ctrl + arrows (word navigation) */
#define KEY_CTRL_UP    0x0128
#define KEY_CTRL_DOWN  0x0129
#define KEY_CTRL_LEFT  0x012A
#define KEY_CTRL_RIGHT 0x012B
#define KEY_CTRL_HOME  0x012C
#define KEY_CTRL_END   0x012D

/* Alt+key (letters/digits only) */
#define KEY_ALT(c)     (0x0200 | ((unsigned char)(c)))  /* Alt+a == 0x0261 */

/* Sentinel values */
#define KEY_CLOSED     -1                /* window was closed */
#define KEY_NONE       -2                /* no key pending (vio_kbhit) */
```

**Example editor loop:**

```c
VGATerm *vt = vgaterm_open("editor");
vio_init(vt);
vio_clrscr();
vio_flush();

for (;;) {
    int k = vio_getch();
    if (k == KEY_CLOSED) break;
    if (k == KEY_CTRL('q')) break;
    
    switch (k) {
        case KEY_UP:    /* handle cursor up */ break;
        case KEY_DOWN:  /* handle cursor down */ break;
        case KEY_CTRL_LEFT: /* word left */ break;
        default:
            if (k >= 32 && k < 127) {
                vio_putch(k);  /* printable ASCII */
            }
    }
    vio_flush();
}

vio_fini();
vgaterm_close(vt);
```

## Font Management & I/O

The `fontio` layer (`fontio.h`) provides utilities for loading and saving 8x16 bitmap fonts in various formats.

- **RAW**: Flat 4096-byte binary (256 glyphs × 16 bytes).
- **PSF1/PSF2**: Linux PC Screen Font formats.
- **C Header**: Generates a `.h` file for static inclusion (like `font_vga.h`).

```c
uint8_t myfont[256][16];
fontio_load("myfont.psf", myfont);
vgaterm_set_font_slot(vt, 1, myfont); /* load into slot 1 */
```

## Font Editor

A built-in font editor (`fontedit.c`) is provided to modify and create CP437-compatible fonts.

```bash
make fontedit
./fontedit
```

### Features:
- **Pixel Editor**: 8x16 grid for detailed glyph modification.
- **Character Map**: View and select from all 256 CP437 characters.
- **Undo**: Simple single-level undo for pixel changes.
- **Format Support**: Saves directly to C headers for easy integration.

## Advanced Usage

### select() / poll() Integration

```c
int fd = vgaterm_fd(vt);
fd_set rfds;
struct timeval tv = { 0, 16667 };   /* 60 fps timeout */

FD_ZERO(&rfds);
FD_SET(fd, &rfds);

if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0)
    vgaterm_events(vt);
```

## Technical Details

### Window Size

Fixed at **640 × 400** pixels (80 cols × 8px, 25 rows × 16px).  The window
manager cannot resize it.

### Font

`font_vga.h` contains 256 glyphs extracted from `Bm437_IBM_VGA_8x16.otb`,
part of the [Ultimate Oldschool PC Font Pack](https://int10h.org/oldschool-pc-fonts/)
by VileR (CC BY-SA 4.0).  This is a faithful recreation of the original IBM
VGA ROM font covering the full CP437 character set — including all box-drawing
characters (single and double line), block elements, and classic symbols
(☺ ☻ ♥ ♦ …).  All 48 box-drawing glyphs in the CP437 range are distinct.

## License

MIT License

Copyright (c) 2026 David Collins

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### Font

`font_vga.h` and `Bm437_IBM_VGA_8x16.otb` are derived from the
[Ultimate Oldschool PC Font Pack](https://int10h.org/oldschool-pc-fonts/)
by VileR, and are licensed separately under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
