#include "kernel.h"
#include "pmm.h"
#include "heap.h"
#include "paging.h"

/*
 * Kernel Heap:
 * Starts at 1MB (0x100000) or higher, backed by pmm_alloc_frames().
 * We use an intrusive header free-list block allocator.
 */

#define HEAP_MAGIC 0x48454150 /* "HEAP" */
#define HEAP_INITIAL_FRAMES 16 /* Initial 64KB heap */

typedef struct heap_block {
    uint32_t magic;
    size_t size;             /* Usable payload size */
    int is_free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

#define BLOCK_HEADER_SIZE (sizeof(heap_block_t))

static heap_block_t *heap_start = NULL;
static size_t total_heap_size = 0;
static size_t used_heap_size = 0;

/* Identity-map freshly allocated heap frames once paging is active, so the
 * heap keeps working when it grows beyond the 16MB identity-mapped region. */
static void heap_map_range(uint32_t paddr, size_t len) {
    if (!vmap_ready()) return;
    for (uint32_t off = 0; off < len; off += PAGE_SIZE)
        vmap(paddr + off, paddr + off, PAGE_WRITE);
}

static int expand_heap(size_t frames) {
    if (frames == 0) return 0;

    /* One contiguous run of frames backing ONE block, so allocations larger
     * than a single 4KB page are actually satisfiable. */
    uint32_t base = pmm_alloc_frames((uint32_t)frames);
    if (!base) return 0;
    heap_map_range(base, frames * PAGE_SIZE);

    heap_block_t *block = (heap_block_t *)base;
    block->magic = HEAP_MAGIC;
    block->size = (uint32_t)(frames * PAGE_SIZE - BLOCK_HEADER_SIZE);
    block->is_free = 1;
    block->next = NULL;
    block->prev = NULL;

    total_heap_size += frames * PAGE_SIZE;

    if (!heap_start) {
        heap_start = block;
    } else {
        heap_block_t *curr = heap_start;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = block;
        block->prev = curr;
    }
    return 1;
}

void heap_init(void) {
    heap_start = NULL;
    total_heap_size = 0;
    used_heap_size = 0;
    expand_heap(HEAP_INITIAL_FRAMES);
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align size to 8 bytes */
    size = (size + 7) & ~7;

    size_t frames_needed = (size + BLOCK_HEADER_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    if (frames_needed < 4) frames_needed = 4;

    for (;;) {
        heap_block_t *curr = heap_start;
        while (curr) {
            if (curr->is_free && curr->size >= size) {
                /* Split block if remainder is big enough */
                if (curr->size >= size + BLOCK_HEADER_SIZE + 16) {
                    heap_block_t *split = (heap_block_t *)((uint8_t *)curr + BLOCK_HEADER_SIZE + size);
                    split->magic = HEAP_MAGIC;
                    split->size = curr->size - size - BLOCK_HEADER_SIZE;
                    split->is_free = 1;
                    split->next = curr->next;
                    split->prev = curr;
                    if (curr->next) curr->next->prev = split;
                    curr->next = split;
                    curr->size = (uint32_t)size;
                }
                curr->is_free = 0;
                used_heap_size += curr->size + BLOCK_HEADER_SIZE;
                return (void *)((uint8_t *)curr + BLOCK_HEADER_SIZE);
            }
            curr = curr->next;
        }

        /* Out of heap: grow by one contiguous multi-frame block, then retry */
        if (!expand_heap(frames_needed)) return NULL;
    }
}

void kfree(void *ptr) {
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - BLOCK_HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) return; /* Invalid pointer */

    block->is_free = 1;
    if (used_heap_size >= block->size + BLOCK_HEADER_SIZE) {
        used_heap_size -= (block->size + BLOCK_HEADER_SIZE);
    }

    /* Coalesce next if contiguous and free */
    if (block->next && block->next->is_free) {
        if ((uint8_t *)block + BLOCK_HEADER_SIZE + block->size == (uint8_t *)block->next) {
            block->size += BLOCK_HEADER_SIZE + block->next->size;
            block->next = block->next->next;
            if (block->next) block->next->prev = block;
        }
    }

    /* Coalesce prev if contiguous and free */
    if (block->prev && block->prev->is_free) {
        if ((uint8_t *)block->prev + BLOCK_HEADER_SIZE + block->prev->size == (uint8_t *)block) {
            block->prev->size += BLOCK_HEADER_SIZE + block->size;
            block->prev->next = block->next;
            if (block->next) block->next->prev = block->prev;
        }
    }
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - BLOCK_HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) return NULL;

    if (block->size >= new_size) {
        return ptr;
    }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, block->size);
    kfree(ptr);
    return new_ptr;
}

size_t heap_used_bytes(void) {
    return used_heap_size;
}

size_t heap_total_bytes(void) {
    return total_heap_size;
}
