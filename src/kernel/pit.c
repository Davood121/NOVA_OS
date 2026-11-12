#include "kernel.h"

#define PIT_HZ 100
#define PIT_DIV 11932

static uint32_t ticks;
static uint16_t prev_cnt;
static int started;

void pit_init(void) {
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(PIT_DIV & 0xFF));
    outb(0x40, (uint8_t)(PIT_DIV >> 8));
    started = 0;
    ticks = 0;
}

void pit_poll(void) {
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    uint16_t cnt = (uint16_t)(lo | ((uint16_t)hi << 8));
    if (started && cnt > prev_cnt) ticks++;
    prev_cnt = cnt;
    started = 1;
}

uint32_t pit_uptime_ms(void) {
    return ticks * (1000 / PIT_HZ);
}
