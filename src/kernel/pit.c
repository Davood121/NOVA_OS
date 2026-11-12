#include "kernel.h"

#define PIT_HZ 100
#define PIT_DIV 11932

static volatile uint32_t ticks;

void pit_init(void) {
    ticks = 0;
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(PIT_DIV & 0xFF));
    outb(0x40, (uint8_t)(PIT_DIV >> 8));
}

void pit_irq(void) {
    ticks++;
}

uint32_t pit_uptime_ms(void) {
    uint32_t t;
    __asm__ volatile("cli");
    t = ticks;
    __asm__ volatile("sti");
    return t * (1000 / PIT_HZ);
}
