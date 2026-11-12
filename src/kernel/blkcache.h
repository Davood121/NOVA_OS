#ifndef GRID_BLKCACHE_H
#define GRID_BLKCACHE_H

#include <stdint.h>
#include <stddef.h>

#define BLK_SECTOR_SIZE 512

/* Initialize the block cache subsystem */
void blkcache_init(void);

/*
 * Read a 512-byte sector from disk/cache.
 * Returns a pointer to the cached sector buffer, or NULL on error.
 */
void *blk_read(uint32_t lba);

/*
 * Write 512 bytes to a sector.
 * Updates the cache, marks it dirty, and schedules or writes through.
 * Returns 0 on success, negative on error.
 */
int blk_write(uint32_t lba, const void *buf);

/* Flush all dirty blocks to the underlying ATA device */
int blk_flush(void);

/* Cache diagnostic stats */
uint32_t blkcache_hits(void);
uint32_t blkcache_misses(void);
uint32_t blkcache_dirty_count(void);
uint32_t blkcache_total_blocks(void);

#endif
