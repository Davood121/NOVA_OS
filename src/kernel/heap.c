#include "kernel.h"
#include "pmm.h"
#include "heap.h"

/*
 * Kernel Heap:
 * Starts at 1MB (0x100000) or higher, backed by pmm_alloc_frame().
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

static int expand_heap(size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame) return 0;
        
        heap_block_t *new_block = (heap_block_t *)frame;
        new_block->magic = HEAP_MAGIC;
        new_block->size = PAGE_SIZE - BLOCK_HEADER_SIZE;
        new_block->is_free = 1;
        new_block->next = NULL;
        new_block->prev = NULL;

        total_heap_size += PAGE_SIZE;

        if (!heap_start) {
            heap_start = new_block;
        } else {
            /* Append to block list */
            heap_block_t *curr = heap_start;
            while (curr->next) {
                curr = curr->next;
            }
            curr->next = new_block;
            new_block->prev = curr;
        }
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
                curr->size = size;
            }
            curr->is_free = 0;
            used_heap_size += curr->size + BLOCK_HEADER_SIZE;
            return (void *)((uint8_t *)curr + BLOCK_HEADER_SIZE);
        }
        if (!curr->next) break;
        curr = curr->next;
    }

    /* Out of heap: allocate more frames from PMM */
    size_t needed = size + BLOCK_HEADER_SIZE;
    size_t frames = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (frames < 4) frames = 4;
    if (!expand_heap(frames)) return NULL;

    return kmalloc(size);
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
