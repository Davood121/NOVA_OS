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
        "  blkstat  - block cache hits/misses/dirty stats\n"
        "  atainfo  - detect ata drive\n"
        "  ataread  - hexdump one disk sector (lba in hex)\n"
        "  ls       - list files on the gridfs store\n"
        "  write    - write text to a file: write <name> <text>\n"
        "  cat      - print a file: cat <name>\n"
        "  rm       - delete a file: rm <name>\n"
        "  fsinfo   - filesystem stats\n"
        "  mkfs     - reformat the disk (erases all files)\n"
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

static void cmd_blkstat(void) {
    console_puts("Block Cache Stats (512B sectors):\n");
    console_puts("  capacity: ");
    console_putdec(blkcache_total_blocks());
    console_puts(" blocks\n  hits:     ");
    console_putdec(blkcache_hits());
    console_puts("\n  misses:   ");
    console_putdec(blkcache_misses());
    console_puts("\n  dirty:    ");
    console_putdec(blkcache_dirty_count());
    console_puts(" blocks\n");
}

static void cmd_atainfo(void) {
    ata_print_info();
}

static void cmd_ataread(const char *args) {
    static uint16_t sector[256];
    uint32_t lba = parse_hex(args);
    if (ata_read_sectors(lba, 1, sector) < 0) {
        console_puts("ataread: read failed (no drive or io error)\n");
        return;
    }
    console_puts("sector ");
    console_puthex32(lba);
    console_puts(":\n");
    const uint8_t *p = (const uint8_t *)sector;
    static const char hexchars[] = "0123456789ABCDEF";
    for (int row = 0; row < 2; row++) {
        console_puthex32(lba * 512 + row * 16);
        console_puts(": ");
        for (int col = 0; col < 16; col++) {
            uint8_t b = p[row * 16 + col];
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

static void cmd_ls(void) {
    fs_print_list();
}

static void cmd_write(char *args) {
    char *text = args;
    while (*text && *text != ' ') text++;
    if (*text) {
        *text = 0;
        text++;
        while (*text == ' ') text++;
    }
    if (!args[0] || !text[0]) {
        console_puts("usage: write <name> <text>\n");
        return;
    }
    uint32_t len = strlen(text);
    if (fs_write(args, text, len) < 0) {
        console_puts("write: failed (disk full or io error)\n");
        return;
    }
    console_puts("wrote ");
    console_putdec(len);
    console_puts(" bytes to '");
    console_puts(args);
    console_puts("'\n");
}

static void cmd_cat(const char *args) {
    if (!args[0]) {
        console_puts("usage: cat <name>\n");
        return;
    }
    if (!fs_exists(args)) {
        console_puts("cat: no such file\n");
        return;
    }
    static uint8_t buf[4096];
    int n = fs_read(args, buf, sizeof buf);
    if (n < 0) {
        console_puts("cat: read failed\n");
        return;
    }
    for (int i = 0; i < n; i++) console_putc((char)buf[i]);
    console_putc('\n');
}

static void cmd_rm(const char *args) {
    if (!args[0]) {
        console_puts("usage: rm <name>\n");
        return;
    }
    if (fs_delete(args) < 0) {
        console_puts("rm: no such file\n");
        return;
    }
    console_puts("deleted '");
    console_puts(args);
    console_puts("'\n");
}

static void cmd_fsinfo(void) {
    fs_print_info();
}

static void cmd_mkfs(const char *args) {
    if (strcmp(args, "yes") != 0) {
        console_puts("mkfs: this erases all files. type 'mkfs yes' to confirm.\n");
        return;
    }
    if (fs_format_force() < 0) {
        console_puts("mkfs: format failed\n");
        return;
    }
    console_puts("mkfs: disk formatted\n");
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
    else if (!strcmp(line, "blkstat")) cmd_blkstat();
    else if (!strcmp(line, "atainfo")) cmd_atainfo();
    else if (!strcmp(line, "ataread")) cmd_ataread(args);
    else if (!strcmp(line, "ls")) cmd_ls();
    else if (!strcmp(line, "write")) cmd_write(args);
    else if (!strcmp(line, "cat")) cmd_cat(args);
    else if (!strcmp(line, "rm")) cmd_rm(args);
    else if (!strcmp(line, "fsinfo")) cmd_fsinfo();
    else if (!strcmp(line, "mkfs")) cmd_mkfs(args);
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
