#include "kernel.h"

#define LINE_MAX 128

static void print_uptime(void) {
    uint32_t ms = pit_uptime_ms();
    console_putdec(ms / 1000);
    console_puts(".");
    console_putdec(ms % 1000);
    console_puts("s\n");
}

static void print_date(void) {
    uint8_t sec, min, hour, day, mon;
    uint16_t year;
    rtc_read(&sec, &min, &hour, &day, &mon, &year);
    console_putdec(day);
    console_puts("/");
    console_putdec(mon);
    console_puts("/");
    console_putdec(year);
    console_puts("  ");
    if (hour < 10) console_putc('0');
    console_putdec(hour);
    console_putc(':');
    if (min < 10) console_putc('0');
    console_putdec(min);
    console_putc(':');
    if (sec < 10) console_putc('0');
    console_putdec(sec);
    console_putc('\n');
}

static void cmd_help(void) {
    console_puts(
        "commands:\n"
        "  help     - this list\n"
        "  clear    - clear screen\n"
        "  echo     - print text\n"
        "  meminfo  - e820 memory map\n"
        "  uptime   - time since boot\n"
        "  date     - real-time clock date and time\n"
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

    if (line[0] == 0) return;
    if (!strcmp(line, "help")) cmd_help();
    else if (!strcmp(line, "clear")) console_clear();
    else if (!strcmp(line, "echo")) cmd_echo(args);
    else if (!strcmp(line, "meminfo")) mem_print();
    else if (!strcmp(line, "uptime")) print_uptime();
    else if (!strcmp(line, "date")) print_date();
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
