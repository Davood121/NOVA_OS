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
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
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
typedef void (*exc_handler_t)(void *regs);
void idt_init(void);
void irq_register(uint8_t irq, irq_handler_t h);
void exc_register(uint8_t exc, exc_handler_t h);
void isr_dispatch(void *regs);

/* ---- keyboard (IRQ-driven) ---- */
char kbd_getchar(void);
void kbd_irq(void);

/* ---- memory (e820) ---- */
void mem_init(void);
void mem_print(void);
uint64_t mem_usable_bytes(void);

/* ---- memory management (v0.3) ---- */
void pmm_init(void);
uint32_t pmm_alloc_frame(void);
uint32_t pmm_alloc_frames(uint32_t count);
void pmm_free_frame(uint32_t paddr);
uint32_t pmm_total_frames(void);
uint32_t pmm_used_frames(void);
uint32_t pmm_free_frames_count(void);

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
size_t heap_used_bytes(void);
size_t heap_total_bytes(void);

void paging_init(void);
int vmap(uint32_t vaddr, uint32_t paddr, uint32_t flags);
int vmap_ready(void);
void vunmap(uint32_t vaddr);
void page_fault_handler(void *regs);

/* ---- ata (pio) ---- */
void ata_init(void);
int ata_present(void);
uint32_t ata_sector_count(void);
const char *ata_model(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);
void ata_print_info(void);

/* ---- block cache (v0.5) ---- */
void blkcache_init(void);
void *blk_read(uint32_t lba);
int blk_write(uint32_t lba, const void *buf);
int blk_flush(void);
uint32_t blkcache_hits(void);
uint32_t blkcache_misses(void);
uint32_t blkcache_dirty_count(void);
uint32_t blkcache_total_blocks(void);

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
