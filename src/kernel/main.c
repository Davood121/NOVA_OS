#include "kernel.h"

static const char banner[] =
    "  ____ ____  ____    _  _____ \n"
    " / ___|  _ \\|  _ \\  / \\|_   _|\n"
    "| |  _| |_) | |_) |/ _ \\ | |  \n"
    "| |_| |  _ <|  _ </ ___ \\| |  \n"
    " \\____|_| \\_\\_| \\_/_/   \\_\\_|  \n";

void kmain(void) {
    console_clear();
    console_set_color(0x0B);
    console_puts(banner);
    console_set_color(0x07);
    console_puts("GRID OS 0.5 - the machine that computes\n\n");

    mem_init();
    pmm_init();
    heap_init();
    paging_init();
    idt_init();
    irq_register(0, pit_irq);
    irq_register(1, kbd_irq);
    pit_init();
    rtc_init();
    ata_init();
    blkcache_init();
    fs_init();
    __asm__ volatile("sti");

    console_puts("boot ok. interrupts live. type 'help' for commands.\n\n");
    shell_run();
}
