#include "kernel.h"

static const char banner[] =
    "  ____ ____  ____    _  _____ \n"
    " / ___|  _ \\|  _ \\  / \|_   _|\n"
    "| |  _| |_) | |_) |/ _ \\ | |  \n"
    "| |_| |  _ <|  _ </ ___ \\| |  \n"
    " \\____|_| \\_\\_| \\_/_/   \\_\\_|  \n";

void kmain(void) {
    console_clear();
    console_set_color(0x0B);
    console_puts(banner);
    console_set_color(0x07);
    console_puts("GRID OS 0.1 - the machine that computes\n\n");

    mem_init();
    pit_init();

    console_puts("boot ok. type 'help' for commands.\n\n");
    shell_run();
}
