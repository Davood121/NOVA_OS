#ifndef GRID_KERNEL_H
#define GRID_KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* ---- io ---- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* ---- console ---- */
void console_clear(void);
void console_set_color(uint8_t c);
void console_putc(char c);
void console_puts(const char *s);
void console_puthex32(uint32_t v);
void console_puthex64(uint64_t v);
void console_putdec(uint32_t v);

/* ---- interrupts ---- */
typedef void (*irq_handler_t)(void);
void idt_init(void);
void irq_register(uint8_t irq, irq_handler_t h);
void isr_dispatch(void *regs);

/* ---- keyboard (IRQ-driven) ---- */
char kbd_getchar(void);
void kbd_irq(void);

/* ---- memory (e820) ---- */
void mem_init(void);
void mem_print(void);
uint64_t mem_usable_bytes(void);

/* ---- pit / time ---- */
void pit_init(void);
void pit_irq(void);
uint32_t pit_uptime_ms(void);

/* ---- rtc ---- */
void rtc_init(void);
void rtc_read(uint8_t *sec, uint8_t *min, uint8_t *hour,
              uint8_t *day, uint8_t *mon, uint16_t *year);

/* ---- cpu ---- */
void cpu_print_info(void);

/* ---- shell ---- */
void shell_run(void);

/* ---- util (freestanding) ---- */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);

#endif
