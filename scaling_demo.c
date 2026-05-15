/*
 * scaling_demo.c -- Demonstration of vgaterm scaling
 * 
 * Shows how to use the new scaling features with interactive keyboard control.
 * Press 1, 2, 4 to change scaling.
 * 
 * Compile with: cc -O2 -o scaling_demo scaling_demo.c vgaterm.c vio.c $(pkg-config --cflags --libs x11)
 */

#include "vgaterm.h"
#include "vio.h"
#include <stdio.h>

int main(void)
{
    VGATerm *vt;
    uint8_t *mem;
    int scale_factor = 1;
    int frame = 0;
    int key;

    /* Open terminal at default 1x scaling */
    vt = vgaterm_open("VGATerm Scaling Demo");
    if (!vt) return 1;

    /* Initialize keyboard input */
    vio_init(vt);

    mem = vgaterm_mem(vt);

    /* Clear screen and show initial message */
    vio_setattr(VGA_ATTR(VGA_BLACK, VGA_LGRAY));
    vio_clrscr();

    vio_gotoxy(2, 1);
    vio_setattr(VGA_ATTR(VGA_BLACK, VGA_YELLOW));
    vio_puts("VGATerm Scaling Demo");

    vio_gotoxy(2, 3);
    vio_setattr(VGA_ATTR(VGA_BLACK, VGA_WHITE));
    vio_puts("Press 'q' to quit");
    
    vio_gotoxy(2, 4);
    vio_puts("Press '1' for 1x scaling");
    
    vio_gotoxy(2, 5);
    vio_puts("Press '2' for 2x scaling");
    
    vio_gotoxy(2, 6);
    vio_puts("Press '4' for 4x scaling");

    vio_flush();

    /* Main loop */
    while (1) {
        char scale_str[64];

        /* Non-blocking key check */
        key = vio_kbhit();
        
        if (key == KEY_CLOSED) break;
        
        if (key != KEY_NONE) {
            /* Handle key presses */
            if (key == 'q' || key == 'Q') break;
            
            if (key == '1') {
                if (vgaterm_setup_scaling(vt, 1) == 0) {
                    scale_factor = 1;
                    vio_clrscr();
                }
            }
            if (key == '2') {
                if (vgaterm_setup_scaling(vt, 2) == 0) {
                    scale_factor = 2;
                    vio_clrscr();
                }
            }
            if (key == '4') {
                if (vgaterm_setup_scaling(vt, 4) == 0) {
                    scale_factor = 4;
                    vio_clrscr();
                }
            }
        }

        /* Update status display */
        vio_gotoxy(2, 10);
        vio_setattr(VGA_ATTR(VGA_BLACK, VGA_CYAN));
        snprintf(scale_str, sizeof(scale_str), "Scale: %dx              ", scale_factor);
        vio_puts(scale_str);

        /* Draw some content for visual testing */
        vio_gotoxy(2, 12);
        vio_setattr(VGA_ATTR(VGA_BLACK, VGA_GREEN));
        vio_puts("Frame: ");
        snprintf(scale_str, sizeof(scale_str), "%d", frame);
        vio_puts(scale_str);

        /* Render color palette (for visual testing) */
        {
            int x, y;
            for (y = 0; y < 2; y++) {
                for (x = 0; x < 16; x++) {
                    uint8_t attr = VGA_ATTR(x, 15);  /* color as background */
                    vio_putch_at(2 + x, 14 + y, 0x20, attr);
                }
            }
        }

        vio_flush();
        frame++;
    }

    vio_fini();
    vgaterm_close(vt);
    return 0;
}
