#include "kernel.h"
#include "pmm.h"
#include "paging.h"

/*
 * Paging subsystem for GRID OS:
 * 2-level x86 page translation:
 * Page Directory (1024 entries of 4 bytes = 4KB)
 * Page Tables (1024 entries of 4 bytes = 4KB each)
 * 
 * We allocate page directories and tables dynamically from PMM.
 */

typedef struct registers_t {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} registers_t;

static uint32_t *kernel_page_dir = NULL;

void page_fault_handler(void *rp) {
    registers_t *r = (registers_t *)rp;
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    console_set_color(0x0C);
    console_puts("\nPAGE FAULT at linear addr: ");
    console_puthex32(cr2);
    console_puts(" (eip=");
    console_puthex32(r->eip);
    console_puts(", err=");
    console_puthex32(r->err_code);
    console_puts(" [");
    if (!(r->err_code & 1)) console_puts("non-present ");
    else console_puts("protection ");
    if (r->err_code & 2) console_puts("write ");
    else console_puts("read ");
    if (r->err_code & 4) console_puts("user");
    else console_puts("kernel");
    console_puts("])\n");
    console_set_color(0x07);

    for (;;) __asm__ volatile("cli; hlt");
}

int vmap(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    if (!kernel_page_dir) return -1;

    uint32_t pd_idx = (vaddr >> 22) & 0x3FF;
    uint32_t pt_idx = (vaddr >> 12) & 0x3FF;

    uint32_t pde = kernel_page_dir[pd_idx];
    uint32_t *pt;

    if (!(pde & PAGE_PRESENT)) {
        /* Allocate a page table from PMM */
        uint32_t pt_frame = pmm_alloc_frame();
        if (!pt_frame) return -1;
        pt = (uint32_t *)pt_frame;
        memset(pt, 0, PAGE_SIZE);
        kernel_page_dir[pd_idx] = pt_frame | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    } else {
        pt = (uint32_t *)(pde & ~0xFFF);
    }

    pt[pt_idx] = (paddr & ~0xFFF) | PAGE_PRESENT | (flags & (PAGE_WRITE | PAGE_USER));

    /* Invalidate TLB for this virtual address */
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    return 0;
}

void vunmap(uint32_t vaddr) {
    if (!kernel_page_dir) return;

    uint32_t pd_idx = (vaddr >> 22) & 0x3FF;
    uint32_t pt_idx = (vaddr >> 12) & 0x3FF;

    uint32_t pde = kernel_page_dir[pd_idx];
    if (!(pde & PAGE_PRESENT)) return;

    uint32_t *pt = (uint32_t *)(pde & ~0xFFF);
    pt[pt_idx] = 0;

    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

void paging_init(void) {
    /* Allocate 4KB page directory */
    uint32_t pd_frame = pmm_alloc_frame();
    if (!pd_frame) {
        console_puts("paging_init: failed to allocate page directory\n");
        return;
    }
    kernel_page_dir = (uint32_t *)pd_frame;
    memset(kernel_page_dir, 0, PAGE_SIZE);

    /*
     * Identity map first 16MB (0x00000000 - 0x01000000).
     * 16MB requires 4 Page Tables (each maps 4MB = 1024 * 4KB).
     */
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t pt_frame = pmm_alloc_frame();
        if (!pt_frame) return;
        uint32_t *pt = (uint32_t *)pt_frame;
        
        for (uint32_t j = 0; j < 1024; j++) {
            uint32_t paddr = (i * 1024 + j) * PAGE_SIZE;
            pt[j] = paddr | PAGE_PRESENT | PAGE_WRITE;
        }

        kernel_page_dir[i] = pt_frame | PAGE_PRESENT | PAGE_WRITE;
    }

    /* Load CR3 with address of Page Directory */
    __asm__ volatile("mov %0, %%cr3" :: "r"(kernel_page_dir));

    /* Enable paging in CR0 (bit 31: PG, bit 0: PE) */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000001; /* Enable PG and PE */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}
