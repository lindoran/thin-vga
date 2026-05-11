# Thin-VGA Documentation

Thin-VGA is a minimal VGA text-mode emulator for X11. It provides a direct 4000-byte memory buffer (emulating physical address `0xB8000`) and an 80×25 text window.

## Architecture

The project is structured into several layers, from low-level X11 interaction to high-level UI utilities.

### 1. Core Layer: `vgaterm`
- **Purpose**: Implements the VGA hardware emulation.
- **Key Files**: `vgaterm.h`, `vgaterm.c`, `font_vga.h`.
- **Responsibilities**:
    - X11 window management (640×400 fixed size).
    - Maintenance of the 4000-byte text buffer (`uint8_t *mem`).
    - Rendering character/attribute pairs using an 8×16 bitmap font.
    - Support for up to 4 font slots (plane-2 emulation) for custom charsets.
    - Blitting the buffer to the screen using `XImage`.
    - **Cursor Blinking**: Managed internally using `clock_gettime`. The cursor state is toggled during `vgaterm_blit()` based on time elapsed.
    - **Font Selection (Plane-2 Emulation)**: Supports 4 simultaneous font slots with per-cell selection via the `fplane` array.
- **API Style**: Imperative, direct memory access.

### 2. I/O Wrapper Layer: `vio`
- **Purpose**: Provides a more ergonomic API for application development.
- **Key Files**: `vio.h`, `vio.c`.
- **Responsibilities**:
    - **Cursor Tracking**: Maintains a "virtual" cursor position and current attribute.
    - **Text Output**: Helpers like `vio_puts`, `vio_putch`, and numeric formatters (`vio_int`, `vio_hex`).
    - **Input Handling**: Maps X11 events to a unified `KEY_*` system, including arrow keys, function keys, and modifiers (Ctrl, Alt).
    - **Drawing**: Rectangular fills, boxes (single and double line).
- **API Style**: Stateful, similar to `conio.h` or basic `ncurses`.

### 3. Utility Layer: `fontio`
- **Purpose**: Handles font file persistence.
- **Key Files**: `fontio.h`, `fontio.c`.
- **Formats Supported**:
    - **RAW**: 4096-byte flat binary (256 glyphs × 16 bytes).
    - **PSF1/PSF2**: Linux console font formats.
    - **H**: C header generation (for `font_vga.h`).

## Build System
The project uses a standard `Makefile`.
- `make`: Builds the static libraries (`libvgaterm.a`, `libvio.a`, `libfontio.a`) and demos.
- `make demo`: Runs the `vgaterm` feature demo.
- `make editor`: Builds and runs the minimal text editor.
- `make fontedit`: Builds and runs the font editor tool.
- `make font`: Regenerates `font_vga.h` using `mkfont.py`.

## Data Layouts

### VGA Memory (4000 Bytes)
```c
uint8_t *mem = vgaterm_mem(vt);
// cell at (col, row):
int offset = (row * 80 + col) * 2;
mem[offset + 0] = character_byte; // CP437
mem[offset + 1] = attribute_byte; // (bg << 4) | fg
```

### Font Data (8x16)
Each glyph is 16 bytes. Each byte represents one row of 8 pixels (MSB is the leftmost pixel).
```c
uint8_t glyph[16];
// bit (7-x) of glyph[y] is the pixel at (x, y)
```

## Key Workflows

### Creating a Basic Application
1. Call `vgaterm_open()`.
2. Get the memory pointer via `vgaterm_mem()`.
3. Modify the buffer.
4. Call `vgaterm_blit()` to update the window.
5. Pump events with `vgaterm_events()` in a loop.

### Using the VIO Layer
1. Call `vgaterm_open()`.
2. Call `vio_init(vt)`.
3. Use `vio_puts()`, `vio_gotoxy()`, etc.
4. Use `vio_getch()` for input.
5. Call `vio_flush()` to update.

## External Dependencies
- **libX11**: Required for windowing and graphics.
- **kbd** (Linux): Required for `mkfont.py` to find system PSF fonts.
- **Python 3**: Required for the font generation script.
