#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE
/* page_init - detect the physical memory and initialize the Page descriptors.
 *
 * The physical memory range is reported by the device tree in physical
 * addresses, while the kernel image, the Page descriptor array and all the
 * variables are addressable through kernel virtual addresses.  The two worlds
 * must never be mixed: address arithmetic on end/pages stays virtual, and every
 * physical memory boundary is obtained through PADDR() before it is compared
 * with the range reported by the device tree. */
static void page_init(void) {
    // virtual address of the end of the kernel image (defined by the linker)
    extern char end[];

    uint64_t mem_base = get_memory_base();  // physical start reported by DTB
    uint64_t mem_size = get_memory_size();  // physical size reported by DTB

    // Case 1: the device tree did not report a usable physical memory range.
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid physical memory range, base=0x%lx size=0x%lx",
              mem_base, mem_size);
    }

    // The kernel (and every kernel virtual address) is mapped with the fixed
    // offset PHYSICAL_MEMORY_OFFSET, i.e. VA = PA + va_pa_offset.
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // Physical end of memory; npage is the upper bound of the manageable
    // physical page numbers, i.e. the page number of that physical end.
    uint64_t mem_end = mem_base + mem_size;
    npage = PPN(mem_end);

    // The Page descriptor array is placed right after the kernel image and is
    // page aligned.  end is a virtual address, so this is pure VA arithmetic.
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    // Only pages [nbase, npage) need a descriptor (page2ppn subtracts nbase).
    // Reserve them all first; the really free pages are released afterwards.
    size_t nr_pages = npage - nbase;
    for (size_t i = 0; i < nr_pages; i++) {
        SetPageReserved(pages + i);
    }

    // First candidate free page: the descriptor array is addressed virtually,
    // so convert its end to a physical address via PADDR before rounding up.
    uintptr_t pages_end_pa = PADDR((uintptr_t)pages + nr_pages * sizeof(struct Page));
    uintptr_t freemem_pa = ROUNDUP(pages_end_pa, PGSIZE);

    // Physical end of the device-tree memory range, rounded down to a page.
    uintptr_t mem_end_pa = ROUNDDOWN(mem_end, PGSIZE);

    cprintf("Physical memory: base=0x%016lx end=0x%016lx size=0x%016lx (%ld MB)\n",
            mem_base, mem_end, mem_size, mem_size / (1024 * 1024));

    // Case 2: there is at least one complete free page after OpenSBI, the
    // kernel image and the Page descriptors.  Case 3: the layout is too tight
    // (freemem_pa >= mem_end_pa), so no page is handed to the pmm and the
    // kernel keeps running without allocatable memory.
    if (freemem_pa < mem_end_pa) {
        size_t n = (mem_end_pa - freemem_pa) / PGSIZE;
        init_memmap(pa2page(freemem_pa), n);
        cprintf("Free physical memory: [0x%016lx, 0x%016lx), %ld pages\n",
                freemem_pa, mem_end_pa, n);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
