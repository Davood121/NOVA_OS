#ifndef GRID_PAGING_H
#define GRID_PAGING_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4

/* Initialize paging: identity map the first 16MB and enable CR0.PG */
void paging_init(void);

/* Map a virtual address to a physical address */
int vmap(uint32_t vaddr, uint32_t paddr, uint32_t flags);

/* Unmap a virtual address */
void vunmap(uint32_t vaddr);

/* Page fault handler */
void page_fault_handler(void *regs);

#endif
