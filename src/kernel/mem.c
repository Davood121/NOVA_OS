#include "kernel.h"

#define E820_ADDR 0x500
#define E820_COUNT (*(volatile uint16_t *)0x4FE)
#define MAX_ENTRIES 24

typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi;
} e820_entry;

void mem_init(void) {}

uint64_t mem_usable_bytes(void) {
    uint64_t total = 0;
    const e820_entry *e = (const e820_entry *)E820_ADDR;
    uint16_t n = E820_COUNT;
    for (uint16_t i = 0; i < n && i < MAX_ENTRIES; i++)
        if (e[i].type == 1) total += e[i].len;
    return total;
}

static const char *type_name(uint32_t t) {
    switch (t) {
        case 1: return "usable";
        case 2: return "reserved";
        case 3: return "ACPI reclaim";
        case 4: return "ACPI NVS";
        case 5: return "bad memory";
        default: return "reserved";
    }
}

void mem_print(void) {
    const e820_entry *e = (const e820_entry *)E820_ADDR;
    uint16_t n = E820_COUNT;
    if (n > MAX_ENTRIES) n = MAX_ENTRIES;
    console_puts("e820 memory map (");
    console_putdec(n);
    console_puts(" entries):\n");
    for (uint16_t i = 0; i < n; i++) {
        console_puts("  ");
        console_puthex64(e[i].base);
        console_puts(" + ");
        console_puthex64(e[i].len);
        console_puts("  ");
        console_puts(type_name(e[i].type));
        console_putc('\n');
    }
    console_puts("total usable: ");
    console_putdec((uint32_t)(mem_usable_bytes() >> 20));
    console_puts(" MB\n");
}
