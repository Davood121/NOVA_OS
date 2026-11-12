#include "kernel.h"

/*
 * PIO ATA driver for the primary channel master drive (LBA28).
 * Polling-based with timeouts; IRQ14 is masked so the PIC never sees
 * drive interrupts we do not handle. This is the storage path the
 * transaction-log filesystem (v0.5) will sit on.
 * Requires the SATA controller in IDE/legacy mode (QEMU default, most
 * BIOSes with "SATA mode: IDE" or "compatibility").
 */

#define ATA_REG_DATA      0x1F0
#define ATA_REG_ERROR     0x1F1
#define ATA_REG_SECCOUNT  0x1F2
#define ATA_REG_LBA_LO    0x1F3
#define ATA_REG_LBA_MID   0x1F4
#define ATA_REG_LBA_HI    0x1F5
#define ATA_REG_DRIVE     0x1F6
#define ATA_REG_STATUS    0x1F7
#define ATA_REG_CONTROL   0x3F6

#define ATA_STATUS_BSY 0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_ERR 0x01

#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_TIMEOUT 200000

static int ata_drive_ok;
static uint32_t ata_sectors;
static char ata_model_str[41];

static void io_wait(void) {
    for (int i = 0; i < 4; i++) inb(0x80);
}

static int wait_not_busy(void) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        if (!(inb(ATA_REG_STATUS) & ATA_STATUS_BSY)) return 1;
        io_wait();
    }
    return 0;
}

static int wait_drq(void) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t st = inb(ATA_REG_STATUS);
        if (st & ATA_STATUS_ERR) return 0;
        if (!(st & ATA_STATUS_BSY) && (st & ATA_STATUS_DRQ)) return 1;
        io_wait();
    }
    return 0;
}

void ata_init(void) {
    ata_drive_ok = 0;
    ata_sectors = 0;
    ata_model_str[0] = 0;

    outb(0xA1, (uint8_t)(inb(0xA1) | 0x40));

    outb(ATA_REG_DRIVE, 0xA0);
    io_wait();

    outb(ATA_REG_SECCOUNT, 0);
    outb(ATA_REG_LBA_LO, 0);
    outb(ATA_REG_LBA_MID, 0);
    outb(ATA_REG_LBA_HI, 0);
    outb(ATA_REG_STATUS, ATA_CMD_IDENTIFY);

    uint8_t st = inb(ATA_REG_STATUS);
    if (st == 0) return;

    if (inb(ATA_REG_LBA_MID) == 0x14 && inb(ATA_REG_LBA_HI) == 0xEB)
        return;

    int ready = 0;
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        st = inb(ATA_REG_STATUS);
        if (st & ATA_STATUS_ERR) return;
        if (!(st & ATA_STATUS_BSY) && (st & ATA_STATUS_DRQ)) {
            ready = 1;
            break;
        }
    }
    if (!ready) return;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_REG_DATA);

    for (int i = 0; i < 20; i++) {
        ata_model_str[i * 2] = (char)(id[27 + i] >> 8);
        ata_model_str[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    ata_model_str[40] = 0;
    for (int i = 39; i >= 0 && ata_model_str[i] == ' '; i--) ata_model_str[i] = 0;

    ata_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    ata_drive_ok = 1;
}

int ata_present(void) { return ata_drive_ok; }
uint32_t ata_sector_count(void) { return ata_sectors; }
const char *ata_model(void) { return ata_model_str; }

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!ata_present() || count == 0) return -1;
    uint16_t *out = buf;

    for (uint8_t s = 0; s < count; s++) {
        if (!wait_not_busy()) return -1;
        outb(ATA_REG_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        outb(ATA_REG_SECCOUNT, 1);
        outb(ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
        outb(ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_REG_STATUS, ATA_CMD_READ_PIO);
        if (!wait_drq()) return -1;
        for (int i = 0; i < 256; i++) out[s * 256 + i] = inw(ATA_REG_DATA);
        lba++;
    }
    return count;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!ata_present() || count == 0) return -1;
    const uint16_t *in = buf;

    for (uint8_t s = 0; s < count; s++) {
        if (!wait_not_busy()) return -1;
        outb(ATA_REG_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        outb(ATA_REG_SECCOUNT, 1);
        outb(ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
        outb(ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_REG_STATUS, ATA_CMD_WRITE_PIO);
        if (!wait_drq()) return -1;
        for (int i = 0; i < 256; i++) outw(ATA_REG_DATA, in[s * 256 + i]);
        outb(ATA_REG_STATUS, ATA_CMD_FLUSH);
        if (!wait_not_busy()) return -1;
        lba++;
    }
    return count;
}

void ata_print_info(void) {
    if (!ata_present()) {
        console_puts("ata: no drive detected on primary master\n");
        return;
    }
    console_puts("ata primary master: ");
    console_puts(ata_model_str);
    console_puts("\n  sectors: ");
    console_putdec(ata_sectors);
    console_puts("\n  size:    ");
    console_putdec(ata_sectors / 2048);
    console_puts(" MB\n");
}
