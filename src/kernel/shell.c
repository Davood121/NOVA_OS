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
        "  memstat  - frame allocator and heap stats\n"
        "  hexdump  - display hex view of memory\n"
        "  sleep    - busy-wait sleep in seconds\n"
        "  rand     - pseudo-random 32-bit integer\n"
        "  uptime   - time since boot\n"
        "  date     - real-time clock date and time\n"
        "  cpuinfo  - cpu vendor and model\n"
        "  reboot   - restart machine\n");
}

static void cmd_memstat(void) {
    console_puts("Physical Frame Allocator (4KB frames):\n");
    console_puts("  total: ");
    console_putdec(pmm_total_frames());
    console_puts(" frames (");
    console_putdec((pmm_total_frames() * 4) / 1024);
    console_puts(" MB)\n  used:  ");
    console_putdec(pmm_used_frames());
    console_puts(" frames\n  free:  ");
    console_putdec(pmm_free_frames_count());
    console_puts(" frames\n\nKernel Heap:\n  total: ");
    console_putdec((uint32_t)(heap_total_bytes() / 1024));
    console_puts(" KB\n  used:  ");
    console_putdec((uint32_t)heap_used_bytes());
    console_puts(" bytes\n");
}

static uint32_t parse_hex(const char *s) {
    uint32_t val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
        else break;
    }
    return val;
}

static void cmd_hexdump(const char *args) {
    uint32_t addr = parse_hex(args);
    if (!addr) addr = 0x8000; /* Default to kernel load address */
    console_puts("Hexdump at ");
    console_puthex32(addr);
    console_puts(":\n");
    const uint8_t *p = (const uint8_t *)addr;
    for (int row = 0; row < 4; row++) {
        console_puthex32(addr + row * 16);
        console_puts(": ");
        for (int col = 0; col < 16; col++) {
            uint8_t b = p[row * 16 + col];
            static const char hexchars[] = "0123456789ABCDEF";
            console_putc(hexchars[(b >> 4) & 0xF]);
            console_putc(hexchars[b & 0xF]);
            console_putc(' ');
        }
        console_puts(" |");
        for (int col = 0; col < 16; col++) {
            char c = (char)p[row * 16 + col];
            if (c >= 32 && c <= 126) console_putc(c);
            else console_putc('.');
        }
        console_puts("|\n");
    }
}

static void cmd_sleep(const char *args) {
    uint32_t sec = 0;
    while (*args >= '0' && *args <= '9') {
        sec = sec * 10 + (*args++ - '0');
    }
    if (sec == 0) sec = 1;
    uint32_t target = pit_uptime_ms() + sec * 1000;
    while (pit_uptime_ms() < target) {
        __asm__ volatile("hlt");
    }
}

static uint32_t lcg_state = 0x12345678;
static void cmd_rand(void) {
    /* Simple LCG (Linear Congruential Generator) */
    lcg_state = lcg_state * 1664525 + 1013904223 + pit_uptime_ms();
    console_puts("0x");
    console_puthex32(lcg_state);
    console_puts(" (");
    console_putdec(lcg_state % 10000);
    console_puts(")\n");
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
    else if (!strcmp(line, "memstat")) cmd_memstat();
    else if (!strcmp(line, "hexdump")) cmd_hexdump(args);
    else if (!strcmp(line, "sleep")) cmd_sleep(args);
    else if (!strcmp(line, "rand")) cmd_rand();
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
