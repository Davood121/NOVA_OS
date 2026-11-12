#include "kernel.h"

#define IRQ_BASE 0x20
#define IRQ_COUNT 16

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct registers {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

typedef void (*isr_stub_t)(void);
extern isr_stub_t isr_stub_table[48];

static irq_handler_t irq_handlers[IRQ_COUNT];

static exc_handler_t exc_handlers[32];

void exc_register(uint8_t exc, exc_handler_t h) {
    if (exc < 32) exc_handlers[exc] = h;
}

void irq_register(uint8_t irq, irq_handler_t h) {
    irq_handlers[irq] = h;
}

static void idt_set_gate(uint8_t n, uint32_t handler) {
    idt[n].base_lo = (uint16_t)(handler & 0xFFFF);
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].base_hi = (uint16_t)(handler >> 16);
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, IRQ_BASE);
    outb(0xA1, IRQ_BASE + 8);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x00);
    outb(0xA1, 0x00);
}

static void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static const char *exc_name(uint32_t n) {
    static const char *names[32] = {
        "divide error", "debug", "NMI", "breakpoint", "overflow",
        "bound range", "invalid opcode", "device not available",
        "double fault", "coprocessor segment", "invalid TSS",
        "segment not present", "stack fault", "general protection fault",
        "page fault", "reserved", "x87 FPU error", "alignment check",
        "machine check", "SIMD FP exception", "virtualization", "control protection",
        0, 0, 0, 0, 0, 0, 0, 0, "security", "reserved"
    };
    return (n < 32 && names[n]) ? names[n] : "reserved";
}

void isr_dispatch(void *rp) {
    struct registers *r = rp;
    if (r->int_no < 32) {
        if (exc_handlers[r->int_no]) {
            exc_handlers[r->int_no](r);
            return;
        }
        console_set_color(0x0C);
        console_puts("\nKERNEL PANIC: exception ");
        console_putdec(r->int_no);
        console_puts(" (");
        console_puts(exc_name(r->int_no));
        console_puts(") err=");
        console_puthex32(r->err_code);
        console_puts(" eip=");
        console_puthex32(r->eip);
        console_putc('\n');
        console_set_color(0x07);
        for (;;) __asm__ volatile("cli; hlt");
    }
    uint8_t irq = (uint8_t)(r->int_no - 32);
    if (irq < IRQ_COUNT && irq_handlers[irq]) irq_handlers[irq]();
    pic_eoi(irq);
}

void idt_init(void) {
    for (int i = 0; i < 48; i++)
        idt_set_gate((uint8_t)i, (uint32_t)isr_stub_table[i]);
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)idt;
    __asm__ volatile("lidt %0" :: "m"(idtp));
    pic_remap();
}
