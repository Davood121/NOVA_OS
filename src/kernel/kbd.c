#include "kernel.h"

#define KB_BUF 128

static const char map_normal[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static const char map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static volatile int shift;
static volatile char buf[KB_BUF];
static volatile uint32_t head, tail;

char kbd_getchar(void) {
    for (;;) {
        __asm__ volatile("cli");
        if (tail != head) {
            char c = buf[tail % KB_BUF];
            tail++;
            __asm__ volatile("sti");
            return c;
        }
        __asm__ volatile("sti; hlt");
    }
}

void kbd_irq(void) {
    uint8_t sc = inb(0x60);
    if (sc & 0x80) {
        uint8_t up = sc & 0x7F;
        if (up == 0x2A || up == 0x36) shift = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift = 1;
        return;
    }
    if (sc >= 128) return;
    char c = shift ? map_shift[sc] : map_normal[sc];
    if (!c) return;
    if ((uint32_t)(head - tail) >= KB_BUF) return;
    buf[head % KB_BUF] = c;
    head++;
}
