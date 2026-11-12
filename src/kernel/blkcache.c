#include "kernel.h"
#include "blkcache.h"

#define CACHE_CAPACITY 64 /* 64 sectors = 32KB cache */

typedef struct blk_node {
    uint32_t lba;
    uint8_t data[BLK_SECTOR_SIZE];
    uint8_t is_valid;
    uint8_t is_dirty;
    uint32_t access_time;
} blk_node_t;

static blk_node_t *cache_table = NULL;
static uint32_t stat_hits = 0;
static uint32_t stat_misses = 0;
static uint32_t access_counter = 0;

void blkcache_init(void) {
    if (!cache_table) {
        cache_table = (blk_node_t *)kmalloc(sizeof(blk_node_t) * CACHE_CAPACITY);
        if (cache_table) {
            memset(cache_table, 0, sizeof(blk_node_t) * CACHE_CAPACITY);
        }
    }
}

static blk_node_t *find_block(uint32_t lba) {
    if (!cache_table) return NULL;
    for (uint32_t i = 0; i < CACHE_CAPACITY; i++) {
        if (cache_table[i].is_valid && cache_table[i].lba == lba) {
            return &cache_table[i];
        }
    }
    return NULL;
}

static blk_node_t *evict_block(void) {
    if (!cache_table) return NULL;

    /* 1. Look for an unused slot */
    for (uint32_t i = 0; i < CACHE_CAPACITY; i++) {
        if (!cache_table[i].is_valid) {
            return &cache_table[i];
        }
    }

    /* 2. Pick least recently used (LRU) slot */
    uint32_t oldest_time = 0xFFFFFFFFU;
    int victim_idx = 0;
    for (uint32_t i = 0; i < CACHE_CAPACITY; i++) {
        if (cache_table[i].access_time < oldest_time) {
            oldest_time = cache_table[i].access_time;
            victim_idx = i;
        }
    }

    blk_node_t *victim = &cache_table[victim_idx];
    if (victim->is_valid && victim->is_dirty) {
        /* Flush dirty victim to ATA disk */
        ata_write_sectors(victim->lba, 1, victim->data);
        victim->is_dirty = 0;
    }

    victim->is_valid = 0;
    return victim;
}

void *blk_read(uint32_t lba) {
    if (!cache_table) blkcache_init();
    if (!cache_table) return NULL;

    blk_node_t *b = find_block(lba);
    if (b) {
        stat_hits++;
        b->access_time = ++access_counter;
        return b->data;
    }

    /* Cache miss */
    stat_misses++;
    b = evict_block();
    if (!b) return NULL;

    if (ata_read_sectors(lba, 1, b->data) < 0) {
        return NULL;
    }

    b->lba = lba;
    b->is_valid = 1;
    b->is_dirty = 0;
    b->access_time = ++access_counter;
    return b->data;
}

int blk_write(uint32_t lba, const void *buf) {
    if (!cache_table) blkcache_init();
    if (!cache_table) return -1;

    blk_node_t *b = find_block(lba);
    if (!b) {
        b = evict_block();
        if (!b) return -1;
        b->lba = lba;
        b->is_valid = 1;
    }

    memcpy(b->data, buf, BLK_SECTOR_SIZE);
    b->is_dirty = 1;
    b->access_time = ++access_counter;
    return 0;
}

int blk_flush(void) {
    if (!cache_table) return 0;
    int flushed = 0;
    for (uint32_t i = 0; i < CACHE_CAPACITY; i++) {
        if (cache_table[i].is_valid && cache_table[i].is_dirty) {
            if (ata_write_sectors(cache_table[i].lba, 1, cache_table[i].data) >= 0) {
                cache_table[i].is_dirty = 0;
                flushed++;
            }
        }
    }
    return flushed;
}

uint32_t blkcache_hits(void) { return stat_hits; }
uint32_t blkcache_misses(void) { return stat_misses; }
uint32_t blkcache_dirty_count(void) {
    if (!cache_table) return 0;
    uint32_t dirty = 0;
    for (uint32_t i = 0; i < CACHE_CAPACITY; i++) {
        if (cache_table[i].is_valid && cache_table[i].is_dirty) dirty++;
    }
    return dirty;
}
uint32_t blkcache_total_blocks(void) { return CACHE_CAPACITY; }
