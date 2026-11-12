#include "kernel.h"

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf));
}

void cpu_print_info(void) {
    uint32_t a, b, c, d, max_ext;
    cpuid(0, &a, &b, &c, &d);
    char vendor[13];
    *(uint32_t *)(vendor + 0) = b;
    *(uint32_t *)(vendor + 4) = d;
    *(uint32_t *)(vendor + 8) = c;
    vendor[12] = 0;
    console_puts("cpu vendor: ");
    console_puts(vendor);
    console_putc('\n');

    cpuid(0x80000000, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000004) {
        char brand[49];
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, &a, &b, &c, &d);
            *(uint32_t *)(brand + i * 16 + 0) = a;
            *(uint32_t *)(brand + i * 16 + 4) = b;
            *(uint32_t *)(brand + i * 16 + 8) = c;
            *(uint32_t *)(brand + i * 16 + 12) = d;
        }
        brand[48] = 0;
        console_puts("cpu model:  ");
        console_puts(brand);
        console_putc('\n');
    }
}
