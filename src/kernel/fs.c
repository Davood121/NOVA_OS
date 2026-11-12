#include "kernel.h"

/*
 * Transaction-log filesystem (v0.5).
 *
 * Disk layout (LBA28, primary master):
 *   LBA 0     superblock
 *   LBA 1-3   file table copy A
 *   LBA 4-6   file table copy B
 *   LBA 7     reserved
 *   LBA 8+    append-only data log
 *
 * Every mutation is a transaction: the new file table is written to the
 * INACTIVE copy, then the superblock is rewritten with the generation bit
 * flipped. A crash between the two writes leaves the old superblock
 * pointing at the old table, so the disk always boots to a consistent
 * state. Data is never overwritten in place (log-structured), so partial
 * writes corrupt nothing that was already committed.
 */

#define FS_SB_LBA        0
#define FS_TABLE_A_LBA   1
#define FS_TABLE_B_LBA   4
#define FS_TABLE_SECTORS 3
#define FS_DATA_START    8

#define FS_MAX_FILES 32
#define FS_NAME_MAX  23

struct fs_super {
    char magic[8];
    uint32_t gen;        /* active table copy: 0 = A, 1 = B */
    uint32_t fs_sectors; /* usable disk size in sectors */
    uint32_t log_next;   /* next free LBA in the data log */
    uint32_t file_count;
};

struct fs_entry {
    char name[FS_NAME_MAX + 1];
    uint32_t size;
    uint32_t start;
    uint32_t sectors;
    uint8_t used;
    uint8_t pad[3];
};

struct fs_table {
    struct fs_entry files[FS_MAX_FILES];
};

static struct fs_super fs_sb;
static struct fs_table fs_tab;
static int fs_ready;

static const char fs_magic[8] = { 'G', 'R', 'I', 'D', 'F', 'S', '0', '1' };

static int magic_ok(const char *m) {
    for (int i = 0; i < 8; i++)
        if (m[i] != fs_magic[i]) return 0;
    return 1;
}

static uint32_t table_lba(uint32_t gen) {
    return gen ? FS_TABLE_B_LBA : FS_TABLE_A_LBA;
}

static struct fs_entry *fs_find_entry(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (fs_tab.files[i].used && !strcmp(fs_tab.files[i].name, name))
            return &fs_tab.files[i];
    }
    return 0;
}

static int fs_commit(void) {
    uint32_t t = table_lba(fs_sb.gen ? 0 : 1);
    if (ata_write_sectors(t, FS_TABLE_SECTORS, &fs_tab) < 0) return -1;
    fs_sb.gen = fs_sb.gen ? 0 : 1;
    if (ata_write_sectors(FS_SB_LBA, 1, &fs_sb) < 0) return -1;
    return 0;
}

static int fs_format(void) {
    memset(&fs_sb, 0, sizeof fs_sb);
    memcpy(fs_sb.magic, fs_magic, 8);
    fs_sb.gen = 0;
    fs_sb.fs_sectors = ata_sector_count();
    fs_sb.log_next = FS_DATA_START;
    fs_sb.file_count = 0;
    memset(&fs_tab, 0, sizeof fs_tab);
    if (ata_write_sectors(FS_TABLE_A_LBA, FS_TABLE_SECTORS, &fs_tab) < 0) return -1;
    if (ata_write_sectors(FS_TABLE_B_LBA, FS_TABLE_SECTORS, &fs_tab) < 0) return -1;
    if (ata_write_sectors(FS_SB_LBA, 1, &fs_sb) < 0) return -1;
    return 0;
}

void fs_init(void) {
    fs_ready = 0;
    if (!ata_present()) {
        console_puts("fs: no ata drive, filesystem disabled\n");
        return;
    }
    struct fs_super sb;
    if (ata_read_sectors(FS_SB_LBA, 1, &sb) < 0) {
        console_puts("fs: superblock read failed\n");
        return;
    }
    if (!magic_ok(sb.magic)) {
        if (fs_format() < 0) {
            console_puts("fs: format failed\n");
            return;
        }
        console_puts("fs: new disk - formatted transaction-log store\n");
    } else {
        fs_sb = sb;
        if (ata_read_sectors(table_lba(fs_sb.gen), FS_TABLE_SECTORS, &fs_tab) < 0) {
            console_puts("fs: file table read failed\n");
            return;
        }
        console_puts("fs: mounted ");
        console_putdec(fs_sb.file_count);
        console_puts(" files\n");
    }
    fs_ready = 1;
}

int fs_create(const char *name) {
    if (!fs_ready || !name[0]) return -1;
    if (fs_find_entry(name)) return -1;
    struct fs_entry *e = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!fs_tab.files[i].used) {
            e = &fs_tab.files[i];
            break;
        }
    }
    if (!e) return -1;
    memset(e, 0, sizeof *e);
    for (int i = 0; i < FS_NAME_MAX && name[i]; i++) e->name[i] = name[i];
    e->used = 1;
    fs_sb.file_count++;
    return fs_commit();
}

int fs_write(const char *name, const void *data, uint32_t len) {
    if (!fs_ready) return -1;
    struct fs_entry *e = fs_find_entry(name);
    if (!e) {
        if (fs_create(name) < 0) return -1;
        e = fs_find_entry(name);
        if (!e) return -1;
    }
    uint32_t sectors = (len + 511) / 512;
    if (fs_sb.log_next + sectors > fs_sb.fs_sectors) return -1;

    static uint8_t secbuf[512];
    for (uint32_t s = 0; s < sectors; s++) {
        uint32_t off = s * 512;
        uint32_t chunk = (len - off > 512) ? 512 : len - off;
        memset(secbuf, 0, 512);
        memcpy(secbuf, (const uint8_t *)data + off, chunk);
        if (ata_write_sectors(fs_sb.log_next + s, 1, secbuf) < 0) return -1;
    }
    e->start = fs_sb.log_next;
    e->sectors = sectors;
    e->size = len;
    fs_sb.log_next += sectors;
    return fs_commit();
}

int fs_read(const char *name, void *buf, uint32_t max) {
    if (!fs_ready) return -1;
    struct fs_entry *e = fs_find_entry(name);
    if (!e) return -1;
    uint32_t len = (e->size < max) ? e->size : max;

    static uint8_t secbuf[512];
    for (uint32_t off = 0; off < len; off += 512) {
        if (ata_read_sectors(e->start + off / 512, 1, secbuf) < 0) return -1;
        uint32_t chunk = (len - off > 512) ? 512 : len - off;
        memcpy((uint8_t *)buf + off, secbuf, chunk);
    }
    return (int)len;
}

int fs_delete(const char *name) {
    if (!fs_ready) return -1;
    struct fs_entry *e = fs_find_entry(name);
    if (!e) return -1;
    e->used = 0;
    fs_sb.file_count--;
    return fs_commit();
}

int fs_exists(const char *name) {
    return fs_ready && fs_find_entry(name) != 0;
}

int fs_format_force(void) {
    if (!ata_present()) return -1;
    if (fs_format() < 0) return -1;
    fs_ready = 1;
    return 0;
}

int fs_ready_state(void) {
    return fs_ready;
}

uint32_t fs_file_count(void) {
    return fs_sb.file_count;
}

uint32_t fs_log_next(void) {
    return fs_sb.log_next;
}

uint32_t fs_capacity_sectors(void) {
    return fs_sb.fs_sectors;
}

void fs_print_info(void) {
    if (!fs_ready) {
        console_puts("fs: not mounted\n");
        return;
    }
    console_puts("gridfs: transaction-log store\n");
    console_puts("  generation: ");
    console_putdec(fs_sb.gen);
    console_puts("  (table ");
    console_putc(fs_sb.gen ? 'B' : 'A');
    console_puts(" active)\n  files:      ");
    console_putdec(fs_sb.file_count);
    console_puts(" / ");
    console_putdec(FS_MAX_FILES);
    console_puts("\n  log head:   lba ");
    console_putdec(fs_sb.log_next);
    console_puts("\n  data used:  ");
    console_putdec(fs_sb.log_next - FS_DATA_START);
    console_puts(" sectors\n  disk size:  ");
    console_putdec(fs_sb.fs_sectors);
    console_puts(" sectors\n");
}

void fs_print_list(void) {
    if (!fs_ready) {
        console_puts("fs: not mounted\n");
        return;
    }
    int any = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        struct fs_entry *e = &fs_tab.files[i];
        if (!e->used) continue;
        console_puts("  ");
        console_puts(e->name);
        console_puts("  ");
        console_putdec(e->size);
        console_puts(" bytes  (");
        console_putdec(e->start);
        console_puts(")\n");
        any = 1;
    }
    if (!any) console_puts("  (empty)\n");
}
