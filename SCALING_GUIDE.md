# VGATerm Scaling Guide

## Overview

The enhanced `vgaterm.c` now supports window scaling (1x, 2x, 4x) for larger display sizes. Scaling is achieved through pixel-doubling (2x) or pixel-quadrupling (4x).

## Features

- **1x scaling**: Original 640×400 window (default)
- **2x scaling**: 1280×800 window with doubled pixels and lines
- **4x scaling**: 2560×1600 window with quadrupled pixels and lines

## API

### New Function

```c
int vgaterm_setup_scaling(VGATerm *vt, int scale_factor);
```

**Parameters:**
- `scale_factor`: 1, 2, or 4

**Returns:** 0 on success, -1 on error

**Notes:**
- Must be called after `vgaterm_open()` and before rendering
- Resizes the window and reallocates the pixel buffer
- To minimize visual artifacts, call this early or use `vgaterm_cls()` after scaling

## Usage Examples

### Basic 2x Scaling
```c
VGATerm *vt = vgaterm_open("My App");
vgaterm_setup_scaling(vt, 2);  // 2x
uint8_t *mem = vgaterm_mem(vt);
// ... render normally ...
```

### 4x Scaling
```c
VGATerm *vt = vgaterm_open("My App");
vgaterm_setup_scaling(vt, 4);  // 4x
uint8_t *mem = vgaterm_mem(vt);
// ... render normally ...
```

### Switching Between Scales
```c
vgaterm_setup_scaling(vt, 1);  // Back to 1x
// ... render ...
vgaterm_setup_scaling(vt, 2);  // Switch to 2x
// ... render ...
```

## Implementation Details

### Rendering with Scaling

The `render_cell()` function handles scaling internally:

1. **Pixel Scaling**: Each glyph pixel is replicated `scale_factor` times horizontally
2. **Line Scaling**: Each font row is replicated `scale_factor` times vertically

### Window Resizing

When scaling is changed:
1. New pixel buffer is allocated
2. New XImage is created
3. X11 window is resized
4. Window size hints are locked to prevent manual resizing

## Performance

Scaling performance depends on the chosen factor:

- **1x**: Original performance (optimal)
- **2x**: ~4x pixel rendering (manageable at typical update rates)
- **4x**: ~16x pixel rendering (still practical for most text-mode applications)

Text-mode applications are generally I/O bound rather than rendering bound, so scaling overhead is typically negligible.

## Backward Compatibility

All existing code continues to work without modification:
- Default is 1x scaling (matches original behavior)
- Old applications don't need to call `vgaterm_setup_scaling()`
- New applications can optionally call it for larger displays
