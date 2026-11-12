#ifndef GRID_HEAP_H
#define GRID_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);

/* Query heap statistics */
size_t heap_used_bytes(void);
size_t heap_total_bytes(void);

#endif
