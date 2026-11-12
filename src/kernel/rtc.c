#include "kernel.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int rtc_updating(void) {
    return cmos_read(0x0A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t v, int bcd) {
    if (!bcd) return v;
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

void rtc_read(uint8_t *sec, uint8_t *min, uint8_t *hour,
              uint8_t *day, uint8_t *mon, uint16_t *year) {
    uint8_t regb, century = 0;
    while (rtc_updating()) {}

    uint8_t s = cmos_read(0x00);
    uint8_t m = cmos_read(0x02);
    uint8_t h = cmos_read(0x04);
    uint8_t d = cmos_read(0x07);
    uint8_t mo = cmos_read(0x08);
    uint8_t y = cmos_read(0x09);
    uint8_t c = cmos_read(0x32);
    regb = cmos_read(0x0B);

    while (rtc_updating()) {}
    if (s != cmos_read(0x00) || m != cmos_read(0x02) || h != cmos_read(0x04) ||
        d != cmos_read(0x07) || mo != cmos_read(0x08) || y != cmos_read(0x09)) {
        while (rtc_updating()) {}
        s = cmos_read(0x00);
        m = cmos_read(0x02);
        h = cmos_read(0x04);
        d = cmos_read(0x07);
        mo = cmos_read(0x08);
        y = cmos_read(0x09);
        c = cmos_read(0x32);
        regb = cmos_read(0x0B);
    }

    int bcd = !(regb & 0x04);
    s = bcd_to_bin(s, bcd);
    m = bcd_to_bin(m, bcd);
    h = bcd_to_bin(h, bcd);
    d = bcd_to_bin(d, bcd);
    mo = bcd_to_bin(mo, bcd);
    y = bcd_to_bin(y, bcd);
    century = bcd_to_bin(c, bcd);

    if (!(regb & 0x02)) {
        int pm = h & 0x80;
        h &= 0x7F;
        if (pm) {
            if (h != 12) h = (uint8_t)(h + 12);
        } else {
            if (h == 12) h = 0;
        }
    }

    *sec = s;
    *min = m;
    *hour = h;
    *day = d;
    *mon = mo;
    *year = (uint16_t)(century >= 19 ? century * 100 + y : 2000 + y);
}

void rtc_init(void) {}
