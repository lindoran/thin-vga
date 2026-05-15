# VGATerm Scaling Implementation Summary

## Overview

Added support for 2x and 4x window scaling to `vgaterm.c` and `vgaterm.h`. The implementation preserves backward compatibility—existing code works unchanged with default 1x scaling.

## Changes Made

### 1. **Struct Enhancements** (`vgaterm.c`)

Added three new fields to `struct VGATerm`:
```c
int          scale_factor; /* 1x, 2x, or 4x scaling                   */
int          current_px_w; /* current pixel width (scaled)             */
int          current_px_h; /* current pixel height (scaled)            */
```

- `scale_factor`: Multiplier for both pixel and line dimensions (1, 2, or 4)
- `current_px_w`, `current_px_h`: Track actual pixel dimensions (640→1280→2560 for width, 400→800→1600 for height)

### 2. **Enhanced Rendering** (`render_cell()`)

Refactored the cell rendering function to support scaling:

**Key changes:**
- Base position is now `col * 8 * scale_factor` and `row * 16 * scale_factor` (was hard-coded)
- Inner loops iterate `scale_factor` times for both pixels and lines
- Uses `vt->current_px_w` instead of hard-coded `VGA_PX_W`

**Scaling algorithm:**
```
For each glyph row (16 rows):
    For each scale_factor repetition:
        For each glyph pixel (8 pixels):
            For each scale_factor horizontal repetition:
                Write the pixel value
```

### 3. **New Public Function** (`vgaterm.h` and `vgaterm.c`)

```c
int vgaterm_setup_scaling(VGATerm *vt, int scale_factor);
```

**Purpose:** Configure window scaling after `vgaterm_open()` but before rendering

**Parameters:**
- `scale_factor`: 1, 2, or 4

**Returns:** 0 on success, -1 on error

**Implementation details:**
- Validates scale factor (must be 1, 2, or 4)
- If scale factor hasn't changed, returns immediately (no-op)
- Allocates new pixel buffer with size `VGA_PX_W*scale * VGA_PX_H*scale`
- Creates new `XImage` with updated dimensions
- Resizes the X11 window
- Updates window size hints to lock the new dimensions
- Properly cleans up old resources

### 4. **Updated Initialization** (`vgaterm_open()`)

**Changes:**
- Initialize scaling fields: `scale_factor=1`, `current_px_w=VGA_PX_W`, `current_px_h=VGA_PX_H`
- Use `vt->current_px_w` and `vt->current_px_h` throughout instead of constants
- Pixel buffer allocation: `vt->current_px_w * vt->current_px_h * sizeof(uint32_t)`
- XImage creation uses dynamic dimensions
- Window creation uses dynamic dimensions

### 5. **Updated Rendering** (`vgaterm_blit()`)

**Changes:**
- `XPutImage()` now uses `vt->current_px_w` and `vt->current_px_h` instead of `VGA_PX_W` and `VGA_PX_H`

### 6. **Updated Event Handling** (`vgaterm_events()`)

**Changes:**
- Expose event handler uses dynamic dimensions for re-blitting

## Scaling Behavior

### 1x Scaling (Default)
- Window: 640×400 pixels
- Each character cell: 8×16 pixels
- No scaling applied

### 2x Scaling
- Window: 1280×800 pixels
- Each character cell: 16×32 pixels
- Each pixel is doubled horizontally and vertically

### 4x Scaling
- Window: 2560×1600 pixels
- Each character cell: 32×64 pixels
- Each pixel is quadrupled horizontally and vertically

## Backward Compatibility

✅ **Fully compatible** with existing code:
- Default behavior unchanged (1x scaling)
- No changes to public API (only additions)
- Existing applications don't need to call `vgaterm_setup_scaling()`
- Can be called at any time (even after rendering has started)

## Files Modified

1. **vgaterm.c**
   - Added struct fields for scaling
   - Rewrote `render_cell()` to support scaling
   - Updated `vgaterm_open()` to use dynamic dimensions
   - Added `vgaterm_setup_scaling()` function
   - Updated `vgaterm_blit()` and event handler

2. **vgaterm.h**
   - Added `vgaterm_setup_scaling()` declaration
   - Added comprehensive documentation comments

## New Files

1. **SCALING_GUIDE.md** - User guide with examples and detailed explanations
2. **scaling_demo.c** - Interactive demonstration program showing scaling usage

## Testing

All changes compile successfully:
```bash
make clean && make  # Builds all targets without warnings
cc -O2 -o scaling_demo scaling_demo.c vgaterm.c vio.c $(pkg-config --cflags --libs x11)
```

## Performance Notes

Text-mode applications are typically I/O bound, not rendering bound, so the overhead is minimal:
- 1x: Baseline performance
- 2x: ~4x pixel writes (negligible for text at typical frame rates)
- 4x: ~16x pixel writes (still practical for text-mode apps)

For a typical 80×25 text display with 10 FPS:
- 1x: ~32,000 pixels/frame
- 2x: ~128,000 pixels/frame
- 4x: ~512,000 pixels/frame

These are all manageable by modern systems.

## Usage Example

```c
VGATerm *vt = vgaterm_open("My Application");

/* Enable 2x scaling for larger display */
vgaterm_setup_scaling(vt, 2);

uint8_t *mem = vgaterm_mem(vt);
vgaterm_cls(vt, VGA_ATTR(VGA_BLACK, VGA_LGRAY));

/* Render normally - scaling is handled automatically */
vgaterm_puts(vt, 10, 10, "Hello, scaled world!", VGA_ATTR(VGA_BLACK, VGA_WHITE));
vgaterm_blit(vt);

while (vgaterm_events(vt)) {
    /* Update display */
    vgaterm_blit(vt);
}

vgaterm_close(vt);
```

## Design Philosophy

- **Minimal overhead**: No unnecessary features or complexity
- **Fast rendering**: Tight inner loops for pixel replication
- **Backward compatible**: Existing code works unchanged
- **Simple API**: Single function call to set scaling
