# vgaterm Makefile
#
# Requires:
#   libx11-dev        (Xlib)
#   kbd or console-setup  (for the PSF font files used by mkfont.py)
#
# Quick start:
#   make          -- build everything (generates font_vga.h if absent)
#   make demo     -- run the demo after building
#   make font     -- (re)generate font_vga.h from system PSF fonts
#   make clean    -- remove build artefacts (keeps font_vga.h)
#   make distclean-- remove everything including font_vga.h
#   make editor   -- very limited implementation of a full screen editor

CC      = cc
CFLAGS  = -O2 -Wall -Wextra -std=c99 -pedantic
LDFLAGS = -lX11 -lm

# If you have pkg-config:
#CFLAGS  += $(shell pkg-config --cflags x11)
#LDFLAGS += $(shell pkg-config --libs   x11)

.PHONY: all demo font clean distclean

all: libvgaterm.a libfontio.a vgaterm_demo editor fontedit scaling_demo

# ---- font header (auto-generated) ----------------------------------------

font_vga.h: mkfont.py
	@echo "Generating font_vga.h from system PSF fonts..."
	python3 mkfont.py > font_vga.h
	@echo "  font_vga.h ready ($(shell wc -l < font_vga.h) lines)"

font: mkfont.py
	python3 mkfont.py > font_vga.h
	@echo "font_vga.h regenerated"

# ---- library --------------------------------------------------------------

vgaterm.o: vgaterm.c vgaterm.h font_vga.h
	$(CC) $(CFLAGS) -c -o $@ $<

libvgaterm.a: vgaterm.o
	ar rcs $@ $^

# ---- demo -----------------------------------------------------------------

demo.o: demo.c vgaterm.h
	$(CC) $(CFLAGS) -c -o $@ $<

vgaterm_demo: demo.o libvgaterm.a
	$(CC) -o $@ $^ $(LDFLAGS)

demo: vgaterm_demo
	./vgaterm_demo

# ---- cleanup --------------------------------------------------------------

clean:
	rm -f *.o libvgaterm.a libvio.a libfontio.a vgaterm_demo demo_test editor fontedit scaling_demo

distclean: clean
	rm -f font_vga.h

# ---- vio layer ------------------------------------------------------------

vio.o: vio.c vio.h vgaterm.h
	$(CC) $(CFLAGS) -c -o $@ $<

libvio.a: vio.o
	ar rcs $@ $^

# ---- fontio ---------------------------------------------------------------

fontio.o: fontio.c fontio.h
	$(CC) $(CFLAGS) -c -o $@ $<

libfontio.a: fontio.o
	ar rcs $@ $^

# ---- fontedit -------------------------------------------------------------

fontedit.o: fontedit.c vio.h vgaterm.h fontio.h font_vga.h
	$(CC) $(CFLAGS) -c -o $@ $<

fontedit: fontedit.o libvio.a libvgaterm.a libfontio.a
	$(CC) -o $@ $^ $(LDFLAGS)

# ---- editor demo ----------------------------------------------------------

editor.o: editor.c vio.h vgaterm.h
	$(CC) $(CFLAGS) -c -o $@ $<

editor: editor.o libvio.a libvgaterm.a
	$(CC) -o $@ $^ $(LDFLAGS)

# ---- scaling demo ---------------------------------------------------------

scaling_demo.o: scaling_demo.c vio.h vgaterm.h
	$(CC) $(CFLAGS) -c -o $@ $<

scaling_demo: scaling_demo.o libvio.a libvgaterm.a
	$(CC) -o $@ $^ $(LDFLAGS)
