#ifndef GRID_PMM_H
#define GRID_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

/* Initialize the physical frame allocator using the BIOS e820 map */
void pmm_init(void);

/* Allocate a single 4KB physical frame. Returns physical address, or 0 on failure. */
uint32_t pmm_alloc_frame(void);

/* Allocate 'count' CONTIGUOUS 4KB physical frames. Returns physical address of the
 * first frame, or 0 on failure. Required for multi-frame kernel allocations. */
uint32_t pmm_alloc_frames(uint32_t count);

/* Free a previously allocated 4KB physical frame */
void pmm_free_frame(uint32_t paddr);

/* Query frame statistics */
uint32_t pmm_total_frames(void);
uint32_t pmm_used_frames(void);
uint32_t pmm_free_frames_count(void);

#endif
