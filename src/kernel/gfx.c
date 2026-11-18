/*
 * gfx.c - VGA mode 13h graphics for GRID OS (v0.7 groundwork).
 *
 * 320x200, 256 colors, linear framebuffer at 0xA0000 (chain 4).
 * The mode is set with raw register writes from protected mode - no BIOS
 * calls needed, works on real hardware and under QEMU's standard VGA.
 *
 * Memory: the framebuffer sits inside the kernel's identity-mapped first
 * 16 MB, so no paging tricks are required to touch it.
 */

#include "kernel.h"

#define GFX_W   320
#define GFX_H   200
#define FB      ((volatile uint8_t *)0xA0000)

static void seq_wr(uint8_t idx, uint8_t val)  { outb(0x3C4, idx); outb(0x3C5, val); }
static void crtc_wr(uint8_t idx, uint8_t val) { outb(0x3D4, idx); outb(0x3D5, val); }
static void gc_wr(uint8_t idx, uint8_t val)   { outb(0x3CE, idx); outb(0x3CF, val); }

/* attribute controller shares one port for index/data; the palette
   flip-flop must be reset with a read of 0x3DA first */
static void atc_wr(uint8_t idx, uint8_t val) {
    (void)inb(0x3DA);
    outb(0x3C0, idx);
    outb(0x3C0, val);
}

/* mode 13h CRTC timings (double-scanned 320x200) */
static const uint8_t crtc13[24] = {
    0x5F, 0x3F, 0x40, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3
};

/* mode 3 (80x25 text) CRTC timings - same values the UEFI loader uses */
static const uint8_t crtc3[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};

/* mode 3 attribute palette (BIOS-compatible text palette) */
static const uint8_t atc3[21] = {
    0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x00,
    0x00
};

void gfx_mode13(void) {
    int i;

    /* misc output: 25.175 MHz clock, positive sync */
    outb(0x3C2, 0x63);

    /* sequencer: reset, 8-dot clock, all planes, chain-4 memory */
    seq_wr(0x00, 0x01);
    seq_wr(0x01, 0x01);
    seq_wr(0x02, 0x0F);
    seq_wr(0x03, 0x00);
    seq_wr(0x04, 0x0E);
    seq_wr(0x00, 0x03);

    /* crtc: unlock write-protect, program timings */
    outb(0x3D4, 0x11);
    outb(0x3D5, (uint8_t)(inb(0x3D5) & 0x7F));
    for (i = 0; i < 24; i++)
        crtc_wr((uint8_t)i, crtc13[i]);

    /* graphics controller: linear A0000 window, 256-color shift mode */
    gc_wr(0x00, 0x00);
    gc_wr(0x01, 0x00);
    gc_wr(0x02, 0x00);
    gc_wr(0x03, 0x00);
    gc_wr(0x04, 0x00);
    gc_wr(0x05, 0x40);
    gc_wr(0x06, 0x05);
    gc_wr(0x07, 0x0F);
    gc_wr(0x08, 0xFF);

    /* attribute: identity palette, 8-bit pixels straight to the DAC */
    for (i = 0; i < 16; i++)
        atc_wr((uint8_t)i, (uint8_t)i);
    atc_wr(0x10, 0x41);
    atc_wr(0x11, 0x00);
    atc_wr(0x12, 0x0F);
    atc_wr(0x13, 0x00);
    atc_wr(0x14, 0x00);
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

void gfx_text_restore(void) {
    int i;

    outb(0x3C2, 0x67);

    seq_wr(0x00, 0x01);
    seq_wr(0x01, 0x00);
    seq_wr(0x02, 0x03);
    seq_wr(0x03, 0x00);
    seq_wr(0x04, 0x02);
    seq_wr(0x00, 0x03);

    outb(0x3D4, 0x11);
    outb(0x3D5, (uint8_t)(inb(0x3D5) & 0x7F));
    for (i = 0; i < 24; i++)
        crtc_wr((uint8_t)i, crtc3[i]);

    gc_wr(0x00, 0x00);
    gc_wr(0x01, 0x00);
    gc_wr(0x02, 0x00);
    gc_wr(0x03, 0x00);
    gc_wr(0x04, 0x00);
    gc_wr(0x05, 0x10);
    gc_wr(0x06, 0x0E);
    gc_wr(0x07, 0x00);
    gc_wr(0x08, 0xFF);

    for (i = 0; i < 21; i++)
        atc_wr((uint8_t)i, atc3[i]);
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

void gfx_putpixel(int x, int y, uint8_t c) {
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H)
        return;
    FB[y * GFX_W + x] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t c) {
    int row, col;
    for (row = y; row < y + h && row < GFX_H; row++)
        for (col = x; col < x + w && col < GFX_W; col++)
            if (col >= 0 && row >= 0)
                FB[row * GFX_W + col] = c;
}

/* deterministic demo picture, used by the shell `demo` command and the
   CI graphics test:
     - rows 0-7: 16 blocks of the standard 16-color palette (8 px tall)
     - rows 8+:  xor gradient, pixel = (x ^ y) & 0xFF       */
void gfx_demo_draw(void) {
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < GFX_W; x++)
            FB[y * GFX_W + x] = (uint8_t)(x / 20);
    for (y = 8; y < GFX_H; y++)
        for (x = 0; x < GFX_W; x++)
            FB[y * GFX_W + x] = (uint8_t)((x ^ y) & 0xFF);
    gfx_fill_rect(10, 12, 40, 12, 0x0C);
}
