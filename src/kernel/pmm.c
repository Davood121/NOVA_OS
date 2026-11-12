#include "kernel.h"
#include "pmm.h"

#define E820_ADDR 0x500
#define E820_COUNT (*(volatile uint16_t *)0x4FE)
#define MAX_E820 24

typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi;
} e820_entry_t;

/*
 * We manage up to 128MB of physical RAM (32768 frames of 4KB).
 * Bitmap: 32768 bits / 32 bits per uint32_t = 1024 uint32_t entries (4096 bytes).
 * Bit = 1 means frame is USED / RESERVED.
 * Bit = 0 means frame is FREE.
 */
#define MAX_FRAMES 32768
#define BITMAP_WORDS (MAX_FRAMES / 32)

static uint32_t frame_bitmap[BITMAP_WORDS];
static uint32_t total_system_frames = 0;
static uint32_t used_frames_count = 0;

static inline void bitmap_set(uint32_t frame) {
    if (frame < MAX_FRAMES) {
        if (!(frame_bitmap[frame / 32] & (1U << (frame % 32)))) {
            frame_bitmap[frame / 32] |= (1U << (frame % 32));
            used_frames_count++;
        }
    }
}

static inline void bitmap_clear(uint32_t frame) {
    if (frame < MAX_FRAMES) {
        if (frame_bitmap[frame / 32] & (1U << (frame % 32))) {
            frame_bitmap[frame / 32] &= ~(1U << (frame % 32));
            used_frames_count--;
        }
    }
}

static inline int bitmap_test(uint32_t frame) {
    if (frame >= MAX_FRAMES) return 1;
    return (frame_bitmap[frame / 32] & (1U << (frame % 32))) != 0;
}

void pmm_init(void) {
    /* Initially mark all frames as used/reserved */
    for (uint32_t i = 0; i < BITMAP_WORDS; i++) {
        frame_bitmap[i] = 0xFFFFFFFFU;
    }
    used_frames_count = MAX_FRAMES;
    total_system_frames = 0;

    const e820_entry_t *e = (const e820_entry_t *)E820_ADDR;
    uint16_t count = E820_COUNT;
    if (count > MAX_E820) count = MAX_E820;

    /* Mark all usable e820 regions as free */
    for (uint16_t i = 0; i < count; i++) {
        if (e[i].type == 1) { /* 1 = usable RAM */
            uint64_t start = e[i].base;
            uint64_t end = start + e[i].len;

            /* Align start up to 4KB and end down to 4KB */
            uint32_t frame_start = (uint32_t)((start + PAGE_SIZE - 1) / PAGE_SIZE);
            uint32_t frame_end = (uint32_t)(end / PAGE_SIZE);

            if (frame_end > MAX_FRAMES) frame_end = MAX_FRAMES;

            for (uint32_t f = frame_start; f < frame_end; f++) {
                bitmap_clear(f);
                if (f >= total_system_frames) {
                    total_system_frames = f + 1;
                }
            }
        }
    }

    /*
     * Protect critical regions (never hand out):
     * 1. 0x00000000 - 0x00100000 (First 1MB):
     *    - BIOS IVT, BDA, e820 at 0x500
     *    - Bootloader at 0x7C00
     *    - Kernel image at 0x8000 - 0x10000
     *    - Kernel stack at 0x74000
     *    - VGA buffer at 0xB8000, BIOS ROM at 0xC0000-0xFFFFF
     */
    uint32_t first_mb_frames = 0x100000 / PAGE_SIZE; /* 256 frames */
    for (uint32_t f = 0; f < first_mb_frames; f++) {
        bitmap_set(f);
    }
}

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < BITMAP_WORDS; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFFU) {
            for (uint8_t bit = 0; bit < 32; bit++) {
                if (!(frame_bitmap[i] & (1U << bit))) {
                    uint32_t frame = i * 32 + bit;
                    if (frame >= total_system_frames) return 0;
                    bitmap_set(frame);
                    return frame * PAGE_SIZE;
                }
            }
        }
    }
    return 0; /* Out of memory */
}

uint32_t pmm_alloc_frames(uint32_t count) {
    if (count == 0) return 0;
    uint32_t run = 0;
    for (uint32_t f = 0; f < total_system_frames; f++) {
        if (!bitmap_test(f)) {
            run++;
            if (run == count) {
                uint32_t start = f + 1 - count;
                for (uint32_t k = start; k <= f; k++) bitmap_set(k);
                return start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return 0; /* No contiguous run large enough */
}

void pmm_free_frame(uint32_t paddr) {
    uint32_t frame = paddr / PAGE_SIZE;
    /* Never free frames in the first 1MB */
    if (frame < (0x100000 / PAGE_SIZE)) return;
    bitmap_clear(frame);
}

uint32_t pmm_total_frames(void) {
    return total_system_frames;
}

uint32_t pmm_used_frames(void) {
    return used_frames_count;
}

uint32_t pmm_free_frames_count(void) {
    if (total_system_frames > used_frames_count) {
        return total_system_frames - used_frames_count;
    }
    return 0;
}
