#include "kernel.h"

#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define COLS 80
#define ROWS 25

static int row, col;
static uint8_t color = 0x07;

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(row * COLS + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

void console_clear(void) {
    for (int i = 0; i < COLS * ROWS; i++)
        VGA_MEM[i] = (uint16_t)(color << 8 | ' ');
    row = 0;
    col = 0;
    update_cursor();
}

static void scroll(void) {
    for (int i = 0; i < (ROWS - 1) * COLS; i++)
        VGA_MEM[i] = VGA_MEM[i + COLS];
    for (int i = (ROWS - 1) * COLS; i < ROWS * COLS; i++)
        VGA_MEM[i] = (uint16_t)(color << 8 | ' ');
    row = ROWS - 1;
}

void console_set_color(uint8_t c) { color = c; }

void console_putc(char c) {
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\b') {
        if (col > 0) col--;
    } else {
        VGA_MEM[row * COLS + col] = (uint16_t)(color << 8 | (uint8_t)c);
        col++;
        if (col >= COLS) {
            col = 0;
            row++;
        }
    }
    if (row >= ROWS) scroll();
    update_cursor();
}

void console_puts(const char *s) {
    while (*s) console_putc(*s++);
}

void console_puthex32(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    console_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        console_putc(hex[(v >> i) & 0xF]);
}

void console_puthex64(uint64_t v) {
    console_puthex32((uint32_t)(v >> 32));
    console_puthex32((uint32_t)v);
}

void console_putdec(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) {
        console_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) console_putc(buf[--i]);
}
