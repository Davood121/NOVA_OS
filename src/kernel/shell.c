#include "kernel.h"

#define LINE_MAX 128

static void print_uptime(void) {
    uint32_t ms = pit_uptime_ms();
    console_putdec(ms / 1000);
    console_puts(".");
    console_putdec(ms % 1000);
    console_puts("s\n");
}

static void cmd_help(void) {
    console_puts(
        "commands:\n"
        "  help     - this list\n"
        "  clear    - clear screen\n"
        "  echo     - print text\n"
        "  meminfo  - e820 memory map\n"
        "  uptime   - time since boot\n"
        "  cpuinfo  - cpu vendor and model\n"
        "  reboot   - restart machine\n");
}

static void cmd_echo(const char *args) {
    console_puts(args);
    console_putc('\n');
}

static void cmd_reboot(void) {
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

static void exec(char *line) {
    char *args = line;
    while (*args && *args != ' ') args++;
    if (*args) {
        *args = 0;
        args++;
        while (*args == ' ') args++;
    }
    pit_poll();

    if (line[0] == 0) return;
    if (!strcmp(line, "help")) cmd_help();
    else if (!strcmp(line, "clear")) console_clear();
    else if (!strcmp(line, "echo")) cmd_echo(args);
    else if (!strcmp(line, "meminfo")) mem_print();
    else if (!strcmp(line, "uptime")) print_uptime();
    else if (!strcmp(line, "cpuinfo")) cpu_print_info();
    else if (!strcmp(line, "reboot")) cmd_reboot();
    else {
        console_puts("unknown command: ");
        console_puts(line);
        console_puts("  (try 'help')\n");
    }
}

void shell_run(void) {
    char line[LINE_MAX];
    int len;
    for (;;) {
        console_puts("grid> ");
        len = 0;
        for (;;) {
            pit_poll();
            char c = kbd_getchar();
            if (c == '\n') {
                console_putc('\n');
                line[len] = 0;
                break;
            }
            if (c == '\b') {
                if (len > 0) {
                    len--;
                    console_putc('\b');
                    console_putc(' ');
                    console_putc('\b');
                }
                continue;
            }
            if (c >= 32 && c < 127 && len < LINE_MAX - 1) {
                line[len++] = c;
                console_putc(c);
            }
        }
        exec(line);
    }
}
